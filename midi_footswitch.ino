#include <MIDI.h>                 // Provides MIDI functionality
#include <JC_Button.h>            // Provides precise button control
#include <ShiftRegister74HC595.h> // Needed to drive 7-segment LED

//#include <Wire.h>             // Needed to drive OLED display
// #include <Adafruit_SSD1306.h> // Needed to drive OLED display
#include <ssd1306.h>

/* Needed for expression logic, will be implemented later
#include <SingleEMAFilterLib.h>
SingleEMAFilter<int> singleEMAFilter(0.3);
bool expressionEnabled = false;
byte expressionCC = 11;
byte expressionChannel = 1;
const byte EXPR_PIN = A0;
*/

MIDI_CREATE_DEFAULT_INSTANCE();

const byte BUTTON_PINS[] = {12, 11, A3, A2, A1, 3, A0};
const byte LED_PINS[] = {7, 6, 5, 4};
#define NUM_LEDS 4

#define SR_NUM_REGISTERS 3
#define SR_SDI_PIN 8
#define SR_SCLK_PIN 9
#define SR_LOAD_PIN 10

const byte SW_RELAYS_USED[] = {1, 2, 3, 6};
const byte SW_RELAYS_UNUSED[] = {4, 5, 7, 8};
#define NUM_RELAYS_USED 4
#define NUM_RELAYS_UNUSED 4

#define LONG_PRESS 600

#define FLICKER_NONE 300
#define FLICKER_SLOW 100
#define FLICKER_FAST 60

Button button1(BUTTON_PINS[0]);
Button button2(BUTTON_PINS[1]);
Button button3(BUTTON_PINS[2]);
Button button4(BUTTON_PINS[3]);
Button button5(BUTTON_PINS[4]);
Button button6(BUTTON_PINS[5]);
Button button7(BUTTON_PINS[6]);

// create shift register object (number of shift registers, data pin, clock pin, latch pin)
ShiftRegister74HC595<SR_NUM_REGISTERS> sr(SR_SDI_PIN, SR_SCLK_PIN, SR_LOAD_PIN);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool command_sent[8] = {0, 0, 0, 0, 0, 0, 0, 0};

const byte numberB[] = {
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

#define MIN_BANK 1
#define MAX_BANK 5

/*
    Channel  1: Eventide ModFactor
    Channel  2: Strymon Iridium
    *Channel  3: Strymon Compadre
    Channel  4: Source Audio Programmable EQ
    *Channel  5: Source Audio Collider
    Channel 16: MIDI Switcher
*/

#define CH_MODFACTOR 1
#define CH_IRIDIUM 2
#define CH_EQ 4
#define CH_SWITCHER 16

/*
 * MIDI Clock things
 */

const byte MIDI_RT_START = 0xFA;
const byte MIDI_RT_STOP = 0xFC;
const byte MIDI_RT_CLOCK = 0xF8;

unsigned long bpm = 120;
unsigned long prevBpm = 120;
unsigned long usPerTick = (unsigned long)(1e6 / (bpm * 24.0 / 60.0));
unsigned long prevTime = 0UL;

unsigned long currentTimer[5] = {1e6, 1e6, 1e6, 1e6, 1e6};
unsigned long lastTap = 0UL;

unsigned long usPerTap = 0UL;
byte tapCounter = 0;

bool midiClockState = false;

void setup()
{

    // Needed for expression logic, will be implemented later
    // analogReference(EXTERNAL);
    // expressionEnabled = false;

    for (byte i = 0; i < NUM_LEDS; i++)
        pinMode(LED_PINS[i], OUTPUT);

    ssd1306_setFixedFont(ssd1306xled_font6x8_AB);
    ssd1306_128x64_i2c_init();

    setDisplay(F("READY")); //ssd1306_printFixedN(0, 8, READY_MSG, STYLE_NORMAL, 1);

    // display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    // delay(500);
    // display.setTextColor(WHITE);
    // // display.cp437(true);
    // display.clearDisplay();
    // setDisplay(F("READY"));
    // display.display();

    MIDI.begin(MIDI_CHANNEL_OMNI);

    button1.begin();
    button2.begin();
    button3.begin();
    button4.begin();
    button5.begin();
    button6.begin();
    button7.begin();

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
        SHORT_7,
        TO_LONG_1,
        LONG_1,
        TO_LONG_2,
        LONG_2,
        TO_LONG_3,
        LONG_3,
        TO_LONG_4,
        LONG_4,
        TO_LONG_7,
        LONG_7
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
    button7.read();

    // Needed for expression logic, will be implemented later
    // if (expressionEnabled)
    // handleExpression(expressionCC, expressionChannel);

    if (tapCounter > 0 && micros() - lastTap > currentTimer[0] * 2)
        tapCounter = 0;

    if (midiClockState)
        handleMidiClock();

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
        else if (button7.wasReleased())
            STATE = SHORT_7;
        else if (button1.pressedFor(LONG_PRESS))
            STATE = TO_LONG_1;
        else if (button2.pressedFor(LONG_PRESS))
            STATE = TO_LONG_2;
        else if (button3.pressedFor(LONG_PRESS))
            STATE = TO_LONG_3;
        else if (button4.pressedFor(LONG_PRESS))
            STATE = TO_LONG_4;
        else if (button7.pressedFor(LONG_PRESS))
            STATE = TO_LONG_7;
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

    case SHORT_7:
        callCommand(7);
        STATE = WAIT;
        break;

    case TO_LONG_1:
        if (!commandMode)
        {
            setDisplay(F("CMD"));
            callCommand(0);
            for (byte i = 0; i < 4; i++)
                command_sent[i] = false;
        }
        else
        {
            setDisplay(F("READY"));
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
        callCommand(8);
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

    case TO_LONG_7:
        if (button7.wasReleased())
            STATE = LONG_7;
        break;

    case LONG_7:
        byte displayPrint[3];
        for (int i = 0; i < 3; i++)
            displayPrint[i] = getNumberToPrint(bpm)[i];
        sr.setAll(displayPrint);

        if (midiClockState)
        {
            MIDI.sendRealTime(MIDI_RT_STOP);
            setDisplay(F("BPM"), F("CLK OFF"));
        }
        else
        {
            MIDI.sendRealTime(MIDI_RT_START);
            setDisplay(F("BPM"), F("CLK ON"));
        }
        midiClockState = !midiClockState;
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

    if (program != 7) // tap
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
    case 8:
        command_8();
        break;
    }

    if (program != 7) // tap
    {
        byte displayPrint[3];
        for (int i = 0; i < 3; i++)
            displayPrint[i] = getNumberToPrint(0, program)[i];
        sr.setAll(displayPrint);
    }
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
        // case 6:
        //     switch (program)
        //     {
        //     case 1:
        //         preset_6_1();
        //         break;
        //     case 2:
        //         preset_6_2();
        //         break;
        //     case 3:
        //         preset_6_3();
        //         break;
        //     case 4:
        //         preset_6_4();
        //         break;
        //     }
        //     break;
        // case 7:
        //     switch (program)
        //     {
        //     case 1:
        //         preset_7_1();
        //         break;
        //     case 2:
        //         preset_7_2();
        //         break;
        //     case 3:
        //         preset_7_3();
        //         break;
        //     case 4:
        //         preset_7_4();
        //         break;
        //     }
        //     break;
        // case 8:
        //     switch (program)
        //     {
        //     case 1:
        //         preset_8_1();
        //         break;
        //     case 2:
        //         preset_8_2();
        //         break;
        //     case 3:
        //         preset_8_3();
        //         break;
        //     case 4:
        //         preset_8_4();
        //         break;
        //     }
        //     break;
        // case 9:
        //     switch (program)
        //     {
        //     case 1:
        //         preset_9_1();
        //         break;
        //     case 2:
        //         preset_9_2();
        //         break;
        //     case 3:
        //         preset_9_3();
        //         break;
        //     case 4:
        //         preset_9_4();
        //         break;
        //     }
        //     break;
    }

    for (byte i = 0; i < 8; i++)
        command_sent[i] = false;
}

void preset_1_1()
{
    MIDI.sendProgramChange(0, CH_IRIDIUM); // Iridium Clean

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CLEAN"), F("GENERIC"));
}

void preset_1_2()
{
    MIDI.sendProgramChange(1, CH_IRIDIUM); // Iridium Crunch

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CRNCH"), F("GENERIC"));
}

void preset_1_3()
{
    MIDI.sendProgramChange(2, CH_IRIDIUM); // Iridium Pedal Platform

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(116, CH_SWITCHER); // 1 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("DRIVE"), F("GENERIC"));
}

void preset_1_4()
{
    MIDI.sendProgramChange(2, CH_IRIDIUM); // Iridium Pedal Platform

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(116, CH_SWITCHER); // 1 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(1 - 1, CH_EQ);      // EQ Lead Boost

    setDisplay(F("LEAD"), F("GENERIC"));
}

void preset_2_1()
{
    MIDI.sendProgramChange(0, CH_IRIDIUM); // Iridium Clean

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(2, CH_MODFACTOR); // ModFactor Chorus
    MIDI.sendProgramChange(5 - 1, CH_EQ);    // EQ Bypass

    setDisplay(F("CLEAN"), F("LOW GAIN"));
}

void preset_2_2()
{
    MIDI.sendProgramChange(0, CH_IRIDIUM); // Iridium Clean

    MIDI.sendProgramChange(111, CH_SWITCHER); // 1 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER); // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("ACSTC"), F("LOW GAIN"));
}

void preset_2_3()
{
    MIDI.sendProgramChange(3, CH_IRIDIUM); // Iridium Drive

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("DRIVE"), F("LOW GAIN"));
}

void preset_2_4()
{
    MIDI.sendProgramChange(3, CH_IRIDIUM); // Iridium Drive

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER); // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(1 - 1, CH_EQ);      // EQ Lead Boost

    setDisplay(F("LEAD"), F("LOW GAIN"));
}

void preset_3_1()
{
    MIDI.sendProgramChange(0, CH_IRIDIUM); // Iridium Clean

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER); // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(1, CH_MODFACTOR); // ModFactor Chorus (80s)
    MIDI.sendProgramChange(5 - 1, CH_EQ);    // EQ Bypass

    setDisplay(F("CLEAN"), F("HI GAIN"));
}

void preset_3_2()
{
    MIDI.sendProgramChange(2, CH_IRIDIUM); // Iridium Pedal Platform

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(116, CH_SWITCHER); // 1 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(2 - 1, CH_EQ);      // EQ TubeScreamer

    setDisplay(F("DRV1"), F("HI GAIN"));
}

void preset_3_3()
{
    MIDI.sendProgramChange(4, CH_IRIDIUM); // Iridium High Gain

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(2 - 1, CH_EQ);      // EQ TubeScreamer

    setDisplay(F("DRV2"), F("HI GAIN"));
}

void preset_3_4()
{
    MIDI.sendProgramChange(4, CH_IRIDIUM); // Iridium High Gain

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER); // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(1, CH_MODFACTOR); // ModFactor Chorus (80s)
    MIDI.sendProgramChange(2 - 1, CH_EQ);    // EQ TubeScreamer

    setDisplay(F("LEAD"), F("HI GAIN"));
}

void preset_4_1()
{
    MIDI.sendProgramChange(5, CH_IRIDIUM); // Iridium Jangly

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER); // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("CLEAN"), F("FUNK/POP"));
}

void preset_4_2()
{
    MIDI.sendProgramChange(5, CH_IRIDIUM); // Iridium Jangly

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER); // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SQSHD"), F("FUNK/POP"));
}

void preset_4_3()
{
    MIDI.sendProgramChange(5, CH_IRIDIUM); // Iridium Jangly

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER); // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(9, CH_MODFACTOR); // ModFactor Auto Filter
    MIDI.sendProgramChange(5 - 1, CH_EQ);    // EQ Bypass

    setDisplay(F("F WAH"), F("FUNK/POP"));
}

void preset_4_4()
{
    MIDI.sendProgramChange(5, CH_IRIDIUM); // Iridium Jangly

    MIDI.sendProgramChange(101, CH_SWITCHER); // 0 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER); // 1 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER); // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER); // 0 BE-OD

    MIDI.sendProgramChange(4, CH_MODFACTOR); // ModFactor Uni-Vibe
    MIDI.sendProgramChange(5 - 1, CH_EQ);    // EQ Bypass

    setDisplay(F("PHASD"), F("FUNK/POP"));
}

void preset_5_1()
{
    MIDI.sendProgramChange(111, CH_SWITCHER);  // 1 Acoustic Sim
    MIDI.sendProgramChange(112, CH_SWITCHER);  // 1 Compressor
    MIDI.sendProgramChange(113, CH_SWITCHER);  // 1 Booster
    MIDI.sendProgramChange(116, CH_SWITCHER);  // 1 BE-OD
    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SWTCH"), F("ALL ON"));
}

void preset_5_2()
{
    MIDI.sendProgramChange(101, CH_SWITCHER);  // 0 Acoustic Sim
    MIDI.sendProgramChange(102, CH_SWITCHER);  // 0 Compressor
    MIDI.sendProgramChange(103, CH_SWITCHER);  // 0 Booster
    MIDI.sendProgramChange(106, CH_SWITCHER);  // 0 BE-OD
    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SWTCH"), F("ALL OFF"));
}

void preset_5_3()
{
    setDisplay(F("EMPTY"), F(""));
}

void preset_5_4()
{
    MIDI.sendProgramChange(0, CH_SWITCHER); // Switcher Reset
    delay(1000);
    // Disable Unused Switcher Relays
    for (byte i = 0; i < NUM_RELAYS_UNUSED; i++)
        MIDI.sendProgramChange(100 + SW_RELAYS_UNUSED[i], CH_SWITCHER);

    MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
    MIDI.sendProgramChange(5 - 1, CH_EQ);      // EQ Bypass

    setDisplay(F("SWTCH"), F("RESET"));
}

// void preset_6_1()
// {
// }

// void preset_6_2()
// {
// }

// void preset_6_3()
// {
// }

// void preset_6_4()
// {
// }

// void preset_7_1()
// {
// }

// void preset_7_2()
// {
// }

// void preset_7_3()
// {
// }

// void preset_7_4()
// {
// }

// void preset_8_1()
// {
// }

// void preset_8_2()
// {
// }

// void preset_8_3()
// {
// }

// void preset_8_4()
// {
// }

// void preset_9_1()
// {
// }

// void preset_9_2()
// {
// }

// void preset_9_3()
// {
// }

// void preset_9_4()
// {
// }

void command_1()
{
    if (!command_sent[1 - 1])
    {
        MIDI.sendProgramChange(2, CH_MODFACTOR); // ModFactor Chorus
        setDisplay(F("CHORS"), F("ON"));
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("CHORS"), F("OFF"));
    }
    command_sent[1 - 1] = !command_sent[1 - 1];
}

void command_2()
{
    if (!command_sent[2 - 1])
    {
        MIDI.sendProgramChange(3, CH_MODFACTOR); // ModFactor Phaser
        setDisplay(F("PHASR"), F("ON"));
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("PHASR"), F("OFF"));
    }
    command_sent[2 - 1] = !command_sent[2 - 1];
}

void command_3()
{
    if (!command_sent[3 - 1])
    {
        MIDI.sendProgramChange(11, CH_MODFACTOR); // ModFactor Rotary
        setDisplay(F("ROTRY"), F("ON"));
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("ROTRY"), F("OFF"));
    }
    command_sent[3 - 1] = !command_sent[3 - 1];
}

void command_4()
{
    if (!command_sent[4 - 1])
    {
        MIDI.sendProgramChange(13, CH_MODFACTOR); // ModFactor Tremolo
        setDisplay(F("TREM"), F("ON"));
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("TREM"), F("OFF"));
    }
    command_sent[4 - 1] = !command_sent[4 - 1];
}

void command_5()
{
    if (!command_sent[5 - 1])
    {
        MIDI.sendProgramChange(5, CH_MODFACTOR); // ModFactor Wah
        setDisplay(F("WAH"), F("ON"));
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("WAH"), F("OFF"));
    }
    command_sent[5 - 1] = !command_sent[5 - 1];
}

void command_6()
{
    if (!command_sent[6 - 1])
    {
        MIDI.sendProgramChange(7, CH_MODFACTOR); // ModFactor Flanger
        setDisplay(F("FLNGR"), F("ON"));
    }
    else
    {
        MIDI.sendProgramChange(125, CH_MODFACTOR); // ModFactor Bypass
        setDisplay(F("FLNGR"), F("OFF"));
    }
    command_sent[6 - 1] = !command_sent[6 - 1];
}

void command_7()
{
    tapTempo();

    byte displayPrint[3];

    if (bpm != prevBpm)
    {
        for (int i = 0; i < 3; i++)
            displayPrint[i] = getNumberToPrint(bpm)[i];
        sr.setAll(displayPrint);
    }

    setDisplay(F("BPM"), F("CLK ON"));
}

void command_8()
{
    if (!command_sent[8 - 1])
    {
        MIDI.sendProgramChange(113, CH_SWITCHER); // Booster
        setDisplay(F("BOOST"), F("ON"));
    }
    else
    {
        MIDI.sendProgramChange(103, CH_SWITCHER); // Booster
        setDisplay(F("BOOST"), F("OFF"));
    }
    command_sent[8 - 1] = !command_sent[8 - 1];
}

/* Adafruit ssd1306 things */

// void setDisplay(const __FlashStringHelper *msg, byte textSize_)
// {
//     display.setTextSize(textSize_);
//     display.clearDisplay();
//     display.setCursor(5, 15);
//     display.print(msg);
//     display.display();
// }

// void setDisplay(const __FlashStringHelper *msg1, const __FlashStringHelper *msg2, byte textSize_)
// {
//     display.setTextSize(textSize_);
//     display.clearDisplay();
//     display.setCursor(5, 5);
//     display.print(msg1);
//     display.setTextSize(textSize_ - 1);
//     display.setCursor(5, 40);
//     display.print(msg2);
//     display.display();
// }


void setDisplay(const __FlashStringHelper *msg)
{
    const char PROGMEM *p = (const char PROGMEM *)msg;
    unsigned char buffer[6] = {};
    for (byte i = 0; i < 5; i++)
    {
        unsigned char c = pgm_read_byte(p++);
        buffer[i] = c;
        if (c == 0)
            break;
    }

    ssd1306_clearScreen();
    ssd1306_printFixedN(2, 20, buffer, STYLE_NORMAL, 2);
}

void setDisplay(const __FlashStringHelper *msg1, const __FlashStringHelper *msg2)
{
    const char PROGMEM *p1 = (const char PROGMEM *)msg1;
    unsigned char buffer1[6] = {};
    for (byte i = 0; i < 5; i++)
    {
        unsigned char c = pgm_read_byte(p1++);
        buffer1[i] = c;
        if (c == 0)
            break;
    }

    const char PROGMEM *p2 = (const char PROGMEM *)msg2;
    unsigned char buffer2[9] = {};
    for (byte i = 0; i < 8; i++)
    {
        unsigned char c = pgm_read_byte(p2++);
        buffer2[i] = c;
        if (c == 0)
            break;
    }

    ssd1306_clearScreen();
    ssd1306_printFixedN(2, 10, buffer1, STYLE_NORMAL, 2);
    ssd1306_printFixedN(2, 50, buffer2, STYLE_NORMAL, 1);
}

void setDisplay(const char *msg1, const __FlashStringHelper *msg2)
{
    const char PROGMEM *p2 = (const char PROGMEM *)msg2;
    unsigned char buffer2[9] = {};
    for (byte i = 0; i < 8; i++)
    {
        unsigned char c = pgm_read_byte(p2++);
        buffer2[i] = c;
        if (c == 0)
            break;
    }

    ssd1306_clearScreen();
    ssd1306_printFixedN(2, 10, msg1, STYLE_NORMAL, 2);
    ssd1306_printFixedN(2, 50, buffer2, STYLE_NORMAL, 1);
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


byte *getNumberToPrint(unsigned int bpm_)
{
    if (bpm_ >= 20 && bpm_ <= 300)
    {
        static byte numberToPrint[3];

        if (bpm_ < 100)
            numberToPrint[0] = B11111111;
        else
            numberToPrint[0] = numberB[(bpm_ / 100) % 10];

        numberToPrint[1] = numberB[(bpm_ / 10) % 10];
        numberToPrint[2] = numberB[bpm_ % 10];

        return numberToPrint;
    }
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

void tapTempo()
{

    if (tapCounter == 0 && !midiClockState)
    {
        MIDI.sendRealTime(MIDI_RT_STOP);
        midiClockState = !midiClockState;
        MIDI.sendRealTime(MIDI_RT_START);
    }

    for (byte i = 4; i > 0; i--)
        currentTimer[i] = currentTimer[i - 1];

    currentTimer[0] = micros() - lastTap;
    lastTap = micros();

    tapCounter++;

    if (tapCounter >= 3)
    {

        byte numAverage = tapCounter - 1;
        unsigned long totalTimer = 0UL;

        for (byte i = 0; i < numAverage; i++)
            totalTimer += currentTimer[i];

        usPerTap = totalTimer / numAverage;
        bpm = (unsigned long)60e6 / usPerTap;
        tapCounter--;
    }
}

void handleMidiClock()
{
    if (bpm != prevBpm)
    {
        usPerTick = (unsigned long)(1e6 / (bpm * 24.0 / 60.0));
        prevBpm = bpm;
    }

    if (micros() - prevTime >= usPerTick)
    {
        prevTime += usPerTick;
        MIDI.sendRealTime(MIDI_RT_CLOCK);
    }
}

/* Needed for expression logic, will be implemented later
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
*/
