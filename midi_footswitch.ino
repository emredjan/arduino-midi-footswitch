#include <MIDI.h>                 // Provides MIDI functionality
#include <JC_Button.h>            // Provides precise button control
#include <ShiftRegister74HC595.h> // Needed to drive 7-segment LED

#include <Wire.h>             // Needed to drive OLED display
#include <Adafruit_SSD1306.h> // Needed to drive OLED display

#include <SingleEMAFilterLib.h>

SingleEMAFilter<int> singleEMAFilter(0.3);

MIDI_CREATE_DEFAULT_INSTANCE();

const byte BUTTON_PINS[] = {12, 11, A3, A2, A1, 3};
const byte LED_PINS[] = {7, 6, 5, 4};
const byte NUM_LEDS = 4;

const byte EXPR_PIN = A0;
const byte EXPR_5V_PIN = 2;

const byte SR_NUM_REGISTERS = 3;
const byte SR_SDI_PIN = 8;
const byte SR_SCLK_PIN = 9;
const byte SR_LOAD_PIN = 10;

const byte SW_RELAYS_USED[] = {1, 2, 3, 6};
const byte SW_RELAYS_UNUSED[] = {4, 5, 7, 8};
const byte NUM_RELAYS_USED = 4;
const byte NUM_RELAYS_UNUSED = 4;

const unsigned long LONG_PRESS = 750;

const unsigned long FLICKER_NONE = 300;
const unsigned long FLICKER_SLOW = 100;
const unsigned long FLICKER_FAST = 60;

Button button1(BUTTON_PINS[0]);
Button button2(BUTTON_PINS[1]);
Button button3(BUTTON_PINS[2]);
Button button4(BUTTON_PINS[3]);
Button button5(BUTTON_PINS[4]);
Button button6(BUTTON_PINS[5]);

// create shift register object (number of shift registers, data pin, clock pin, latch pin)
ShiftRegister74HC595<SR_NUM_REGISTERS> sr(SR_SDI_PIN, SR_SCLK_PIN, SR_LOAD_PIN);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool command_sent[7] = {0, 0, 0, 0, 0, 0, 0};

bool expressionEnabled = false;
byte expressionCC = 100;
byte expressionChannel = 2;

byte numberB[] = {
    B11000000, //0
    B11111001, //1
    B10100100, //2
    B10110000, //3
    B10011001, //4
    B10010010, //5
    B10000010, //6
    B11111000, //7
    B10000000, //8
    B10010000  //9
};

const byte MIN_BANK = 1;
const byte MAX_BANK = 5;

/*
    Channel  1: Eventide ModFactor
    Channel  2: Strymon Iridium
    *Channel  3: Strymon Compadre
    Channel  4: Source Audio Programmable EQ
    *Channel  5: Source Audio Collider
    Channel 16: MIDI Switcher
*/

const byte CH_MODFACTOR = 1;
const byte CH_IRIDIUM = 2;
const byte CH_EQ = 4;
const byte CH_SWITCHER = 16;

void setup()
{

    analogReference(DEFAULT);
    expressionEnabled = false;

    for (byte i = 0; i < NUM_LEDS; i++)
        pinMode(LED_PINS[i], OUTPUT);

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    delay(500);
    display.setTextColor(WHITE);
    display.cp437(true);
    display.clearDisplay();
    setDisplay(F("READY"), 4);
    display.display();

    MIDI.begin(MIDI_CHANNEL_OMNI);

    button1.begin();
    button2.begin();
    button3.begin();
    button4.begin();
    button5.begin();
    button6.begin();

    // Only enable Serial for USB MIDI debugging
    //Serial.begin(9600);

    // Disable Unused Switcher Relays
    for (byte i = 0; i < NUM_RELAYS_UNUSED; i++)
        MIDI.sendProgramChange(100 + SW_RELAYS_UNUSED[i], CH_SWITCHER);

    // Start by displaying a dash
    byte displayPrint[3] = {B10111111, B10111111, B10111111};
    sr.setAll(displayPrint);
}

void loop()
{

    enum states_t
    {
        WAIT,
        SHORT_1,
        SHORT_2,
        SHORT_3,
        SHORT_4,
        SHORT_5,
        SHORT_6,
        TO_LONG_1,
        LONG_1,
        TO_LONG_2,
        LONG_2,
        TO_LONG_3,
        LONG_3,
        TO_LONG_4,
        LONG_4
    };
    static states_t STATE;

    static byte bankNum = 1;
    static byte newBankNum = 0;

    static bool commandMode = false;

    button1.read();
    button2.read();
    button3.read();
    button4.read();
    button5.read();
    button6.read();

    if (expressionEnabled)
        handleExpression(expressionCC, expressionChannel);

    switch (STATE)
    {
    case WAIT:
        if (button1.wasReleased())
            STATE = SHORT_1;
        else if (button2.wasReleased())
            STATE = SHORT_2;
        else if (button3.wasReleased())
            STATE = SHORT_3;
        else if (button4.wasReleased())
            STATE = SHORT_4;
        else if (button5.wasReleased())
            STATE = SHORT_5;
        else if (button6.wasReleased())
            STATE = SHORT_6;
        else if (button1.pressedFor(LONG_PRESS))
            STATE = TO_LONG_1;
        else if (button2.pressedFor(LONG_PRESS))
            STATE = TO_LONG_2;
        else if (button3.pressedFor(LONG_PRESS))
            STATE = TO_LONG_3;
        else if (button4.pressedFor(LONG_PRESS))
            STATE = TO_LONG_4;
        break;

    case SHORT_1:
        if (commandMode)
            callCommand(1);
        else
            callPreset(bankNum, 1);
        STATE = WAIT;
        break;

    case SHORT_2:
        if (commandMode)
            callCommand(2);
        else
            callPreset(bankNum, 2);
        STATE = WAIT;
        break;

    case SHORT_3:
        if (commandMode)
            callCommand(3);
        else
            callPreset(bankNum, 3);
        STATE = WAIT;
        break;

    case SHORT_4:
        if (commandMode)
            callCommand(4);
        else
            callPreset(bankNum, 4);
        STATE = WAIT;
        break;

    case SHORT_5:
        callCommand(5);
        STATE = WAIT;
        break;

    case SHORT_6:
        callCommand(6);
        STATE = WAIT;
        break;

    case TO_LONG_1:
        if (!commandMode)
        {
            setDisplay(F("CMD"), 5);
            callCommand(0);
            for (byte i = 0; i < 4; i++)
                command_sent[i] = false;
        }
        else
        {
            setDisplay(F("READY"), 4);
            callPreset(bankNum, 0);
        }

        if (button1.wasReleased())
            STATE = LONG_1;
        break;

    case LONG_1:
        commandMode = !commandMode;
        STATE = WAIT;
        break;

    case TO_LONG_2:
        callCommand(0);
        if (button2.wasReleased())
            STATE = LONG_2;
        break;

    case LONG_2:
        callCommand(7);
        STATE = WAIT;
        break;

    case TO_LONG_3:
        if (commandMode)
        {
            if (button3.wasReleased())
                callCommand(3);
            STATE = WAIT;
        }
        else
        {
            newBankNum = min(bankNum + 1, MAX_BANK);
            callPreset(newBankNum, 0);
            if (button3.wasReleased())
                STATE = LONG_3;
        }
        break;

    case LONG_3:
        bankNum = newBankNum;
        STATE = WAIT;
        break;

    case TO_LONG_4:
        if (commandMode)
        {
            if (button4.wasReleased())
                callCommand(4);
            STATE = WAIT;
        }
        else
        {
            newBankNum = max(bankNum - 1, MIN_BANK);
            callPreset(newBankNum, 0);
            if (button4.wasReleased())
                STATE = LONG_4;
        }
        break;

    case LONG_4:
        bankNum = newBankNum;
        STATE = WAIT;
        break;
    }
}

void callCommand(byte program)
{

    for (byte i = 0; i < NUM_LEDS; i++)
    {
        if ((i == program - 1) && program != 0)
            digitalWrite(LED_PINS[i], HIGH);
        else
            digitalWrite(LED_PINS[i], LOW);
    }

    msgFlicker(FLICKER_FAST, 5, getNumberToPrint(0, program));

    switch (program)
    {
    case 1:
        command_1();
        break;
    case 2:
        command_2();
        break;
    case 3:
        command_3();
        break;
    case 4:
        command_4();
        break;
    case 5:
        command_5();
        break;
    case 6:
        command_6();
        break;
    case 7:
        command_7();
        break;
    }

    byte displayPrint[3];
    for (int i = 0; i < 3; i++)
        displayPrint[i] = getNumberToPrint(0, program)[i];
    sr.setAll(displayPrint);
}

void callPreset(byte bank, byte program)
{
    for (byte i = 0; i < NUM_LEDS; i++)
    {
        if ((i == program - 1) && program != 0)
            digitalWrite(LED_PINS[i], HIGH);
        else
            digitalWrite(LED_PINS[i], LOW);
    }

    msgFlicker(FLICKER_FAST, 5, getNumberToPrint(bank, program));

    byte displayPrint[3];
    for (int i = 0; i < 3; i++)
        displayPrint[i] = getNumberToPrint(bank, program)[i];
    sr.setAll(displayPrint);

    switch (bank)
    {
    case 1:
        switch (program)
        {
        case 1:
            preset_1_1();
            break;
        case 2:
            preset_1_2();
            break;
        case 3:
            preset_1_3();
            break;
        case 4:
            preset_1_4();
            break;
        }
        break;
    case 2:
        switch (program)
        {
        case 1:
            preset_2_1();
            break;
        case 2:
            preset_2_2();
            break;
        case 3:
            preset_2_3();
            break;
        case 4:
            preset_2_4();
            break;
        }
        break;
    case 3:
        switch (program)
        {
        case 1:
            preset_3_1();
            break;
        case 2:
            preset_3_2();
            break;
        case 3:
            preset_3_3();
            break;
        case 4:
            preset_3_4();
            break;
        }
        break;
    case 4:
        switch (program)
        {
        case 1:
            preset_4_1();
            break;
        case 2:
            preset_4_2();
            break;
        case 3:
            preset_4_3();
            break;
        case 4:
            preset_4_4();
            break;
        }
        break;
    case 5:
        switch (program)
        {
        case 1:
            preset_5_1();
            break;
        case 2:
            preset_5_2();
            break;
        case 3:
            preset_5_3();
            break;
        case 4:
            preset_5_4();
            break;
        }
        break;
    case 6:
        switch (program)
        {
        case 1:
            preset_6_1();
            break;
        case 2:
            preset_6_2();
            break;
        case 3:
            preset_6_3();
            break;
        case 4:
            preset_6_4();
            break;
        }
        break;
    case 7:
        switch (program)
        {
        case 1:
            preset_7_1();
            break;
        case 2:
            preset_7_2();
            break;
        case 3:
            preset_7_3();
            break;
        case 4:
            preset_7_4();
            break;
        }
        break;
    case 8:
        switch (program)
        {
        case 1:
            preset_8_1();
            break;
        case 2:
            preset_8_2();
            break;
        case 3:
            preset_8_3();
            break;
        case 4:
            preset_8_4();
            break;
        }
        break;
    case 9:
        switch (program)
        {
        case 1:
            preset_9_1();
            break;
        case 2:
            preset_9_2();
            break;
        case 3:
            preset_9_3();
            break;
        case 4:
            preset_9_4();
            break;
        }
        break;
    }

    for (byte i = 0; i < 7; i++)
        command_sent[i] = false;

}

void preset_1_1()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER);  // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CLEAN"), F("GENRC"), 4);
}

void preset_1_2()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CRNCH"), F("GENRC"), 4);
}

void preset_1_3()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER);  // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(2 - 1, CH_EQ);      // EQ MID Drop

    setDisplay(F("DRIVE"), F("GENRC"), 4);
}

void preset_1_4()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(113, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(1 - 1, CH_EQ);      // EQ Lead Boost

    setDisplay(F("LEAD"), F("GENRC"), 4);
}

void preset_2_1()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(113, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CLEAN"), F("LOW G"), 4);
}

void preset_2_2()
{
    MIDI.sendProgramChange(111, CH_SWITCHER);  // 1 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("ACSTC"), F("LOW G"), 4);
}

void preset_2_3()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("DRIVE"), F("LOW G"), 4);
}

void preset_2_4()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(113, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(1 - 1, CH_EQ);      // EQ Lead Boost

    setDisplay(F("LEAD"), F("LOW G"), 4);
}

void preset_3_1()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(113, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CLEAN"), F("HI G"), 4);
}

void preset_3_2()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CRNCH"), F("HI G"), 4);
}

void preset_3_3()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER);  // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(116, CH_SWITCHER);  // 1 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(2 - 1, CH_EQ);      // EQ Mid Drop

    setDisplay(F("DRIVE"), F("HI G"), 4);
}

void preset_3_4()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(113, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(116, CH_SWITCHER);  // 1 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(1 - 1, CH_EQ);      // EQ Lead Boost

    setDisplay(F("LEAD"), F("HI G"), 4);
}

void preset_4_1()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER);  // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(3 - 1, CH_EQ);      // EQ Sparkle

    setDisplay(F("CLEAN"), F("F/POP"), 4);
}

void preset_4_2()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(0, CH_MODFACTOR);   // ModFactor Volume Pedal
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SQSHD"), F("F/POP"), 4);
}

void preset_4_3()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(9, CH_MODFACTOR);   // ModFactor Auto Filter
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("F WAH"), F("F/POP"), 4);
}

void preset_4_4()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(4, CH_MODFACTOR);   // ModFactor Uni-Vibe
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("PHASD"), F("F/POP"), 4);
}

void preset_5_1()
{
    MIDI.sendProgramChange(111, CH_SWITCHER);  // 1 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(113, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(116, CH_SWITCHER);  // 1 BE-OD
    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SWTCH"), F("ALLON"), 4);
}

void preset_5_2()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER);  // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SWTCH"), F("ALLOFF"), 4);
}

void preset_5_3()
{
    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass
}

void preset_5_4()
{
    MIDI.sendProgramChange(0, CH_SWITCHER);    // Switcher Reset
    delay(1000);
    // Disable Unused Switcher Relays
    for (byte i = 0; i < NUM_RELAYS_UNUSED; i++)
        MIDI.sendProgramChange(100 + SW_RELAYS_UNUSED[i], CH_SWITCHER);

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SWTCH"), F("RESET"), 4);
}

void preset_6_1()
{
}

void preset_6_2()
{
}

void preset_6_3()
{
}

void preset_6_4()
{
}

void preset_7_1()
{
}

void preset_7_2()
{
}

void preset_7_3()
{
}

void preset_7_4()
{
}

void preset_8_1()
{
}

void preset_8_2()
{
}

void preset_8_3()
{
}

void preset_8_4()
{
}

void preset_9_1()
{
}

void preset_9_2()
{
}

void preset_9_3()
{
}

void preset_9_4()
{
}

void command_1()
{
    if (!command_sent[1 - 1])
    {
        MIDI.sendProgramChange(2, CH_MODFACTOR);   // ModFactor Chorus
        setDisplay(F("CHORS"), F("ON"), 4);
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("CHORS"), F("OFF"), 4);
    }
    command_sent[1 - 1] = !command_sent[1 - 1];
}

void command_2()
{
    if (!command_sent[2 - 1])
    {
        MIDI.sendProgramChange(3, CH_MODFACTOR);   // ModFactor Phaser
        setDisplay(F("PHASR"), F("ON"), 4);
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("PHASR"), F("OFF"), 4);
    }
    command_sent[2 - 1] = !command_sent[2 - 1];
}

void command_3()
{
    if (!command_sent[3 - 1])
    {
        MIDI.sendProgramChange(11, CH_MODFACTOR);  // ModFactor Rotary
        setDisplay(F("ROTRY"), F("ON"), 4);
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("ROTRY"), F("OFF"), 4);
    }
    command_sent[3 - 1] = !command_sent[3 - 1];
}

void command_4()
{
    if (!command_sent[4 - 1])
    {
        MIDI.sendProgramChange(13, CH_MODFACTOR);  // ModFactor Tremolo
        setDisplay(F("TREM"), F("ON"), 4);
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("TREM"), F("OFF"), 4);
    }
    command_sent[4 - 1] = !command_sent[4 - 1];
}

void command_5()
{
    if (!command_sent[5 - 1])
    {
        MIDI.sendProgramChange(5, CH_MODFACTOR);   // ModFactor Wah
        setDisplay(F("WAH"), F("ON"), 4);
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("WAH"), F("OFF"), 4);
    }
    command_sent[5 - 1] = !command_sent[5 - 1];
}

void command_6()
{
    if (!command_sent[6 - 1])
    {
        MIDI.sendProgramChange(7, CH_MODFACTOR);  // ModFactor Flanger
        setDisplay(F("FLNGR"), F("ON"), 4);
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("FLNGR"), F("OFF"), 4);
    }
    command_sent[6 - 1] = !command_sent[6 - 1];
}

void command_7()
{
    if (!command_sent[7 - 1])
    {
        MIDI.sendProgramChange(113, CH_SWITCHER); // Booster
        setDisplay(F("BOOST"), F("ON"), 4);
    }
    else
    {
        MIDI.sendProgramChange(103, CH_SWITCHER); // Booster
        setDisplay(F("BOOST"), F("OFF"), 4);
    }
    command_sent[7 - 1] = !command_sent[7 - 1];
}


void setDisplay(const __FlashStringHelper *msg, byte textSize_)
{
    display.setTextSize(textSize_);
    display.clearDisplay();
    display.setCursor(5, 15);
    display.print(msg);
    display.display();
}

void setDisplay(const __FlashStringHelper *msg1, const __FlashStringHelper *msg2, byte textSize_)
{
    display.setTextSize(textSize_);
    display.clearDisplay();
    display.setCursor(5, 5);
    display.print(msg1);
    display.setTextSize(textSize_ - 1);
    display.setCursor(5, 40);
    display.print(msg2);
    display.display();
}

byte *getNumberToPrint(byte bank, byte program)
{
    static byte numberToPrint[3];

    if (bank == 0)
        numberToPrint[0] = B11000110;
    else
        numberToPrint[0] = numberB[bank];

    // Print a dash in the middle
    numberToPrint[1] = B10111111;

    if (program == 0)
        // Print a dash at the end if just a bank change command
        numberToPrint[2] = B10111111;
    else
        numberToPrint[2] = numberB[program];

    return numberToPrint;
}

void msgFlicker(long flickerTime, int flickerCount, byte *message)
{
    unsigned long previousBlink = 0;
    bool flickState = false;
    int timesFlickered = 0;

    while (timesFlickered < flickerCount)
    {
        if (millis() - previousBlink >= flickerTime)
        {
            flickState = !flickState;
            if (flickState)
            {
                sr.setAll(message);
            }
            else
            {
                sr.setAllHigh();
            }

            previousBlink = millis();

            timesFlickered++;
        }
    }
}

void handleExpression(byte controller, byte channel)
{
    static uint8_t previousValue = 0b10000000;

    uint8_t analogValue = analogRead(EXPR_PIN) / 8;

    singleEMAFilter.AddValue(analogValue);

    uint8_t CC_value = singleEMAFilter.GetLowPass();

    if (CC_value != previousValue)
    {
        MIDI.sendControlChange(controller, CC_value, channel);
        previousValue = CC_value;
    }
}

// uint8_t hysteresisFilter(uint16_t inputLevel)
// {

//     const uint16_t margin = 4;                 //  +/- 2
//     const uint16_t numberOfLevelsOutput = 128; // 128 => 0..127  ( power of 2 ideally  )

//     // number of discrete input values.
//     // 8 bit ADC = 256, 9 bit ADC = 512, 10 bit ADC = 1024, 11 bit ADC = 2048, 12 bit ADC = 4096
//     const uint16_t numberOfInputValues = 1024; //  0..1023 (power of 2 ideally )

//     // initial output level (usually zero)
//     const uint16_t initialOutputLevel = 0;
//     // ========================================

//     // the current output level is retained for the next calculation.
//     // Note: initial value of a static variable is set at compile time.
//     static uint16_t currentOutputLevel = initialOutputLevel;

//     // get lower and upper bounds for currentOutputLevel
//     uint16_t lb = (float)((float)numberOfInputValues / numberOfLevelsOutput) * currentOutputLevel;
//     if (currentOutputLevel > 0)
//         lb -= margin; // subtract margin

//     uint16_t ub = (((float)((float)numberOfInputValues / numberOfLevelsOutput) * (currentOutputLevel + 1)) - 1);
//     if (currentOutputLevel < numberOfLevelsOutput)
//         ub += margin; // add margin
//                       // now test if input is outside the outer margins for current output value
//                       // If so, calculate new output level.
//     if (inputLevel < lb || inputLevel > ub)
//     {
//         // determine new output level
//         currentOutputLevel = (((float)inputLevel * (float)numberOfLevelsOutput) / numberOfInputValues);
//     }
//     return currentOutputLevel;
// }
