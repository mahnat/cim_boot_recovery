#include "mbed.h"
#include "TextLCD.h"
#include <cstdio>

I2C i2c_lcd(PB_7,PB_6); // SDA, SCL
 
TextLCD_I2C lcd(&i2c_lcd, PCF8574_SA7, TextLCD::LCD20x4);  // I2C bus, PCF8574 Slaveaddress, LCD Type

DigitalIn uButton(USER_BUTTON);
DigitalOut led1(LED1);
char buffer[50];

DigitalInOut cimReset(PA_4);
DigitalIn cimStatus(PA_5, PullUp);

DigitalOut cimCLK(PA_6);
DigitalIn cimRX(PA_2, PullDown);
DigitalOut cimTX(PA_3);

uint8_t ArrECU[1024];

static UnbufferedSerial pc(SERIAL_TX, SERIAL_RX, 115200);

void printBuf(uint8_t *buf, uint32_t len)
{
    printf("Len %u\r\n", (uint16_t)len);
    for (uint32_t i = 0; i < len; i++)
    {
        printf("%02x ", buf[i]);
    }
    printf("\r\n");
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

// START = 0
// DATA = bits from data
// ELSE = 1
//
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
    //lcd.ClearStringLine(7);
    lcd.locate(0, 1);
    lcd.printf("                    ");
    lcd.setUDC(0, (char *) udc_PO);

	switch(statusbyte) {
        case 0: 
            printf("Key status: Not verified\r\n");
            //lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Not verified");
            lcd.locate(0, 1);
            lcd.putc(0); 
            lcd.printf(" Not verified");
            break;
        case 1: 
            printf("Key status: Verification failed\r\n");
            //lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Verification failed");
            lcd.locate(0, 1);
            lcd.putc(0); 
            lcd.printf(" Verification failed");
            break;
        case 2: 
            printf("Key status: Unknown status 2\r\n");
            //lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Unknown status 2");
            lcd.locate(0, 1);
            lcd.putc(0); 
            lcd.printf(" Unknown status 2");
            break;
        case 3: 
            printf("Key status: Verified\r\n");
            //lcd.DisplayStringAtLine(7, (uint8_t *)"Key status: Verified");
            lcd.locate(0, 1);
            lcd.putc(0); 
            lcd.printf(" Verified");
            break;
	}
}

void printVersion()
{
    //lcd.DisplayStringAtLine(4, (uint8_t *)"Version");
    lcd.locate(0, 0);
    lcd.printf("Version ");

    uint8_t sendVer[2] = {0xFB, 0 };
    // 56 45 52 2e 34 2e 30 31
    // Read version
    ArrECU[ sendCommand_len(sendVer, 1, ArrECU, 8) ] = 0;

    printBuf(ArrECU, 8);
    printf("%s\r\n", ArrECU);
    
    //lcd.DisplayStringAtLine(5, (uint8_t *)ArrECU);
    lcd.locate(8, 0);
    //lcd.printf(ArrECU);
    sprintf(buffer, "%s", ArrECU);
    lcd.printf(buffer);
}

void printStatusRegs()
{
	uint8_t sendStatus[] = { 0x70, 0x00 };

    // Read status register
    printf("\r\nChecking current status...\r\n");
    sendCommand_len(sendStatus,  1, ArrECU, 2);
    //if (ArrECU[1] != 0x06 || ArrECU[0] != 0x80)
    printBuf(ArrECU, 2);

    printStatus(ArrECU[1]);
}

void cimPowerCycle(int seconds)
{
    
    //lcd.ClearStringLine(11);
    lcd.locate(0, 3);
    lcd.printf("                    ");
    //lcd.DisplayStringAtLine(11, (uint8_t *)"Toggle CIM power DOWN");
    lcd.locate(0, 3);
    lcd.printf("Turn CIM power DOWN");
    printf("Toggle CIM power down\r\n");
    while(!cimStatus);

    cimReset.output();
    cimReset = 0;
    cimTX=0;
    cimCLK=0;
    cimStatus.mode(PullDown);

    //lcd.ClearStringLine(5);
    // version string dont need to be cleaned from 2004 LCD
    //lcd.ClearStringLine(7);
    lcd.locate(0, 1);
    lcd.printf("                    ");
    
    for(int timeout = seconds + 1; timeout > 0; timeout--) {
        //lcd.ClearStringLine(11);
        lcd.locate(0, 3);
        lcd.printf("                    ");
        sprintf(buffer, "Security wait %d secs", timeout-1);
        //lcd.DisplayStringAtLine(11, (uint8_t *)buffer);
        lcd.locate(0, 3);
        lcd.printf(buffer);
        printf("Wait!\r\n");
        wait_us(1000000);
    }

    cimTX=1;
    cimCLK=1;
    cimReset.input();
    cimStatus.mode(PullUp);
    
    //lcd.ClearStringLine(11);
    lcd.locate(0, 3);
    lcd.printf("                    ");
    //lcd.DisplayStringAtLine(11, (uint8_t *)"Toggle CIM power UP");
    lcd.locate(0, 3);
    lcd.printf("Turn CIM power UP");
    printf("Toggle CIM power up\r\n");
    while(cimStatus);
    
    //lcd.ClearStringLine(11);
    lcd.locate(0, 3);
    lcd.printf("                    ");

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

    //lcd.ClearStringLine(8);
    lcd.locate(0, 2);
    lcd.printf("                    ");
	while (currKey < len)
	{
        sprintf(buffer, "%d/%d %02x%02x%02x%02x%02x%02x%02x]", currKey, len, 
            keyPtr[0], keyPtr[1], keyPtr[2], keyPtr[3], keyPtr[4], keyPtr[5], keyPtr[6]);  
        //lcd.DisplayStringAtLine(8, (uint8_t *)buffer);
        lcd.locate(0, 2);
        lcd.printf(buffer);
        
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

				printf("Key[%u] = %02x (S: %u, L: %u, A: %u)\n\r", currKey, keyPtr[currKey], shorts, longs, (longs+shorts)/2);

				currKey++;
			}
		}
	};

    //lcd.ClearStringLine(8);
    lcd.locate(0, 2);
    lcd.printf("                    ");

    sprintf(buffer, "Key: %02x%02x%02x%02x%02x%02x%02x",
        keyPtr[0], keyPtr[1], keyPtr[2], keyPtr[3], keyPtr[4], keyPtr[5], keyPtr[6]);  
    //lcd.DisplayStringAtLine(8, (uint8_t *)buffer);
    lcd.locate(0, 2);
    lcd.printf(buffer);

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
        
        //lcd.DisplayStringAtLine(11, (uint8_t *)"Checking...");
        lcd.locate(0, 3);
        lcd.printf("Checking...");
        printBuf(cmd, 5 + len);

        sendCommand_len(cmd, 5 + len, 0, 0);

        // Read status register
        uint8_t sendStatus[] = { 0x70, 0x00 };
        sendCommand_len(sendStatus,  1, ArrECU, 2);
        printStatus(ArrECU[1]);
    
        //lcd.ClearStringLine(11);
        lcd.locate(0, 3);
        lcd.printf("                    ");

    } while((ArrECU[1] & 0x0c) != 12 || ArrECU[0] != 0x80);
    
    printBuf(ArrECU, 2);

    //lcd.DisplayStringAtLine(11, (uint8_t *)"Checking... DONE");
    lcd.locate(0, 3);
    lcd.printf("Checking... DONE");
    printf("Done\r\n");
    while(!uButton);
    while(uButton);
}


int main()
{
    uint8_t sendKey[] = {
            0xF5, 0xDF, 0xff, 0x0f, 0x07,
            0x8b, 0x8e, 0x17, 0x3b, 0x2f, 0xec, 0xb8
    };

    printf("RESET\r\n");
    cimReset.output();
    cimReset.mode(OpenDrain);

    // Enable DWT Timer
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    //DWT->LAR = 0xC5ACCE55; 
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    led1 = 1;

    lcd.cls();
    lcd.setBacklight(TextLCD::LightOn);

    //lcd.DisplayStringAtLine(0, (uint8_t *)"SAAB CIM bootloader key cracker");
    lcd.locate(0, 0);
    lcd.printf("SAAB CIM key cracker");

    sprintf(buffer, "CPU is %d Hz", SystemCoreClock);  
    //lcd.DisplayStringAtLine(1, (uint8_t *)buffer);
    lcd.locate(0, 1);
    lcd.printf(buffer);

    resetTarget();

    wait_us(2000000);
    
    //lcd.DisplayStringAtLine(11, (uint8_t *)"Turn CIM power UP...");
    lcd.locate(0, 3);
    lcd.printf("Turn CIM power UP...");
    while(cimStatus.read());

    //lcd.ClearStringLine(11);
    //lcd.DisplayStringAtLine(2, (uint8_t *)"Starting...");
    lcd.locate(0, 3);
    lcd.printf("Starting...");

    while(1)
    {
        led1=0;
        for(unsigned int line = 0; line < 4; line++) {
            //lcd.ClearStringLine(line);
            lcd.locate(0, line);
            lcd.printf("                    ");
        }
    
        resetTarget();
        led1=1;
        
        printVersion();
        
        printStatusRegs();
        
        // Perform cracking
        printf("\n\rCracking the main part of the key\r\n");
        crackKey(sendKey, 7);

        printf("Waiting for user input\r\n");

        wait_us(5000000);
        while(!uButton);
    }
}
/*
int main() {

    lcd.cls();
    lcd.locate(0, 0);
    lcd.printf("Hello world!\n");
    lcd.printf("01 23 45 67 AB CD EF");

    
// Show cursor as blinking character
    lcd.setCursor(TextLCD::CurOff_BlkOn);
    lcd.setBacklight(TextLCD::LightOn);
 
// Set and show user defined characters. A maximum of 8 UDCs are supported by the HD44780.
// They are defined by a 5x7 bitpattern. 
    lcd.setUDC(0, (char *) udc_Bat_Hi);  // Show |>
    lcd.putc(0);    
    lcd.setUDC(1, (char *) udc_4);  // Show <|
    lcd.putc(1);    

}
*/