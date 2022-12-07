#include "mbed.h"
#include "LCD_DISCO_F746NG.h"
#include "stm32746g_discovery_lcd.h"

LCD_DISCO_F746NG lcd;

DigitalIn uButton(USER_BUTTON);
DigitalOut led1(LED1);
char buffer[50];

DigitalInOut cimReset(D10);
DigitalIn cimStatus(D9, PullUp);

DigitalOut cimCLK(D13);
DigitalIn cimRX(D12, PullDown);
DigitalOut cimTX(D11);

uint8_t ArrECU[1024];

Serial pc(SERIAL_TX, SERIAL_RX, 115200);

void printBuf(uint8_t *buf, uint32_t len)
{
    pc.printf("Len %u\r\n", (uint16_t)len);
    for (uint32_t i = 0; i < len; i++)
    {
        pc.printf("%02x ", buf[i]);
    }
    pc.printf("\r\n");
}

void slightDelay()
{
    for (volatile uint32_t i = 0; i < 400; i++)
    {
        asm volatile("nop");
    }
}

void resetCust(uint32_t pre, uint32_t post) {
    cimReset.output();
    cimReset = 0;
    wait_us(pre);

    // Set clock high
    cimCLK = 1;
    // Data out high
    cimTX = 1;

    cimReset = 1;
    cimReset.input();
    wait_us(post);
}

void resetTarget() {
    resetCust(4, 3);
}

void resetTargetLong() {
    resetCust(1000, 150);
}

/*
 START = 0
 DATA = bits from data
 ELSE = 1
 */
uint8_t shiftBits(uint8_t bits) {
	uint8_t outval = 0;

	// Wait for target ready
	while(cimStatus.read());

	// "START" Data goes low
	cimTX = 0;
    slightDelay();

	for (uint32_t i = 0; i < 8; i++)
	{
		// Clock low
		cimCLK = 0;

        cimTX = (bits & 1);

		bits>>=1;

		slightDelay();

		outval >>= 1;
		outval |= cimRX.read() ? 0x80 : 0;
		// Clock high
		cimCLK = 1;

		slightDelay();

		// Target captures on rising edge
		// We read on falling
	}

	// "ELSE" Data & clock goes high
	cimTX = 1;
	cimCLK = 1;

	return outval;
}

uint32_t sendCommand_len(uint8_t *cmd, uint8_t len, uint8_t *retBuf, uint32_t expLen)
{
    uint32_t retLen = 0;

    while(len--)
    {
        // Wait for target to become ready (low == ready)
        while(cimStatus.read());

        // Send data
        shiftBits(*cmd++);
    }

    while (expLen != 0)
    {
        while(cimStatus.read());

		*retBuf++ = shiftBits(0xff);

        expLen--;
        retLen++;
    }

    return retLen;
}

void printStatus(uint8_t statusbyte)
{
	statusbyte = (statusbyte >> 2)&3;
    lcd.ClearStringLine(7);
	switch(statusbyte) {
        case 0: 
            pc.printf("Key status: Not verified\r\n");
            lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Not verified");
            break;
        case 1: 
            pc.printf("Key status: Verification failed\r\n");
            lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Verification failed");
            break;
        case 2: 
            pc.printf("Key status: Unknown status 2\r\n");
            lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Unknown status 2");
            break;
        case 3: 
            pc.printf("Key status: Verified\r\n");
            lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Verified");
            break;
	}
}

void printVersion()
{
    lcd.DisplayStringAtLine(4, (uint8_t *)"Version");

    uint8_t sendVer[2] = {0xFB, 0 };
    // 56 45 52 2e 34 2e 30 31
    // Read version
    ArrECU[ sendCommand_len(sendVer, 1, ArrECU, 8) ] = 0;

    printBuf(ArrECU, 8);
    pc.printf("%s\r\n", ArrECU);
    
    lcd.DisplayStringAtLine(5, (uint8_t *)ArrECU);
}

void printStatusRegs()
{
	uint8_t sendStatus[] = { 0x70, 0x00 };

    // Read status register
    pc.printf("\r\nChecking current status...\r\n");
    sendCommand_len(sendStatus,  1, ArrECU, 2);
    /*if (ArrECU[1] != 0x06 || ArrECU[0] != 0x80) */
    printBuf(ArrECU, 2);

    printStatus(ArrECU[1]);
}

void cimPowerCycle(int seconds)
{
    
    lcd.ClearStringLine(11);
    lcd.DisplayStringAtLine(11, (uint8_t *)"Toggle CIM power DOWN");
    pc.printf("Toggle CIM power down\r\n");
    while(!cimStatus);

    cimReset.output();
    cimReset = 0;
    cimTX=0;
    cimCLK=0;
    cimStatus.mode(PullDown);

    lcd.ClearStringLine(5);
    lcd.ClearStringLine(7);

    for(int timeout = seconds + 1; timeout > 0; timeout--) {
        lcd.ClearStringLine(11);
        sprintf(buffer, "Security wait timeout... %d seconds", timeout-1);
        lcd.DisplayStringAtLine(11, (uint8_t *)buffer);
        pc.printf("Wait!\r\n");
        wait(1);
    }

    cimTX=1;
    cimCLK=1;
    cimReset.input();
    cimStatus.mode(PullUp);
    
    lcd.ClearStringLine(11);
    lcd.DisplayStringAtLine(11, (uint8_t *)"Toggle CIM power UP");
    pc.printf("Toggle CIM power up\r\n");
    while(cimStatus);
    
    lcd.ClearStringLine(11);

	resetTargetLong();

	printVersion();
    printStatusRegs();
}

void crackKey(uint8_t *cmd, uint8_t len)
{
	static uint32_t longest[256] = {0};
	uint8_t *keyPtr = &cmd[5];
	uint32_t newCnt, currCnt, accTime = 0;
	uint32_t step = 0;
	uint16_t kw = 0;
	uint16_t currKey = 0;

	cmd[4] = len;

	// Init key to some garbage
	for (uint32_t i = 0; i < len; i++)
		keyPtr[i] = 0x00;

    lcd.ClearStringLine(8);
	while (currKey < len)
	{
        sprintf(buffer, "Key: CRACKING %d/%d [%02x:%02x:%02x:%02x:%02x:%02x:%02x]", currKey, len, 
            keyPtr[0], keyPtr[1], keyPtr[2], keyPtr[3], keyPtr[4], keyPtr[5], keyPtr[6]);  
        lcd.DisplayStringAtLine(8, (uint8_t *)buffer);
        
		// resetTarget();
		asm volatile("cpsid if");

		sendCommand_len(cmd, 5 + len, 0, 0);
		currCnt = DWT->CYCCNT;

		// Wait for target completion
		do { newCnt = DWT->CYCCNT; } while(cimStatus);

		// Enable ints
		asm volatile("cpsie if");

		// Target spent this many host cycles to complete the request
		currCnt = (newCnt-currCnt);

		accTime+=currCnt;
		if (++step == 3)
		{
			longest[keyPtr[currKey]] = accTime;
			keyPtr[currKey]++;
			kw++;
			accTime = 0;
			step = 0;

			if (kw == 256)
			{
				kw = 0;
				uint32_t currentRec = longest[ keyPtr[currKey] ];

				for (uint32_t i = 0; i < 256; i++)
				{
                    if (longest[i] > currentRec)
                    {
                        currentRec = longest[i];
                        keyPtr[currKey] = (uint8_t)i;
                    }
                }
				uint32_t shorts = 0xffffffff;
				uint32_t longs = 0;
				for (uint32_t i = 0; i < 256; i++)
				{
					if (longest[i] > longs)  longs = longest[i];
					if (longest[i] < shorts) shorts = longest[i];
				}

				pc.printf("Key[%u] = %02x (S: %u, L: %u, A: %u)\n\r", currKey, keyPtr[currKey], shorts, longs, (longs+shorts)/2);

				currKey++;
			}
		}
	};

    lcd.ClearStringLine(8);
    sprintf(buffer, "Key: %02x:%02x:%02x:%02x:%02x:%02x:%02x",
        keyPtr[0], keyPtr[1], keyPtr[2], keyPtr[3], keyPtr[4], keyPtr[5], keyPtr[6]);  
    lcd.DisplayStringAtLine(8, (uint8_t *)buffer);

	resetTargetLong();
	wait_us(500);
	sendCommand_len(cmd, 5 + len, 0, 0);

    // Read status register
	uint8_t sendStatus[] = { 0x70, 0x00 };
    sendCommand_len(sendStatus,  1, ArrECU, 2);

    if ((ArrECU[1]&0x0c) != 4 || ArrECU[0] != 0x80) printBuf(ArrECU, 2);
    printStatus(ArrECU[1]);

    do {
        cimPowerCycle(5);
        
        lcd.DisplayStringAtLine(11, (uint8_t *)"Checking...");
        printBuf(cmd, 5 + len);

        sendCommand_len(cmd, 5 + len, 0, 0);

        // Read status register
        uint8_t sendStatus[] = { 0x70, 0x00 };
        sendCommand_len(sendStatus,  1, ArrECU, 2);
        printStatus(ArrECU[1]);
    
        lcd.ClearStringLine(11);
    } while((ArrECU[1] & 0x0c) != 12 || ArrECU[0] != 0x80);
    
    printBuf(ArrECU, 2);

    lcd.DisplayStringAtLine(11, (uint8_t *)"Checking... DONE");
    pc.printf("Done\r\n");
    while(!uButton);
    while(uButton);
}


int main()
{
    uint8_t sendKey[] = {
            0xF5, 0xDF, 0xff, 0x0f, 0x07,
            0x8b, 0x8e, 0x17, 0x3b, 0x2f, 0xec, 0xb8
    };

    pc.printf("RESET\r\n");
    cimReset.output();
    cimReset.mode(OpenDrain);

    // Enable DWT Timer
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55; 
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    led1 = 1;

    lcd.SetFont(&Font16);
    lcd.Clear(LCD_COLOR_DARKGRAY);
    lcd.SetBackColor(LCD_COLOR_DARKGRAY);
    lcd.SetTextColor(LCD_COLOR_WHITE);

    lcd.DisplayStringAtLine(0, (uint8_t *)"SAAB CIM bootloader key cracker");

    sprintf(buffer, "CPU is %d Hz", SystemCoreClock);  
    lcd.DisplayStringAtLine(1, (uint8_t *)buffer);

    resetTarget();

    wait(2);
    
    lcd.DisplayStringAtLine(11, (uint8_t *)"Turn CIM power UP...");
    while(cimStatus.read());

    lcd.ClearStringLine(11);
    lcd.DisplayStringAtLine(2, (uint8_t *)"Starting...");
  
    while(1)
    {
        led1=0;
        for(unsigned int line = 4; line < 20; line++) {
            lcd.ClearStringLine(line);
        }
    
        resetTarget();
        led1=1;
        
        printVersion();
        
        printStatusRegs();
        
        // Perform cracking
        pc.printf("\n\rCracking the main part of the key\r\n");
        crackKey(sendKey, 7);

        pc.printf("Waiting for user input\r\n");

        wait(5);
        while(!uButton);
    }
}
