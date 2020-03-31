#include <MIDI.h>
#include <JC_Button.h>
#include <ShiftRegister74HC595.h>

#include <Wire.h>
#include <Adafruit_SSD1306.h>

MIDI_CREATE_DEFAULT_INSTANCE();

const byte BUTTON_PINS[] = {12, 11, A3, A2};
const byte LED_PINS[] = {7, 6, 5, 4};
const byte NUM_BUTTONS = 4;

const byte SR_NUM_REGISTERS = 3;
const byte SR_SDI_PIN = 8;
const byte SR_SCLK_PIN = 9;
const byte SR_LOAD_PIN = 10;

const unsigned long LONG_PRESS = 750;

const unsigned long FLICKER_NONE = 300;
const unsigned long FLICKER_SLOW = 100;
const unsigned long FLICKER_FAST = 60;

Button button1(BUTTON_PINS[0]);
Button button2(BUTTON_PINS[1]);
Button button3(BUTTON_PINS[2]);
Button button4(BUTTON_PINS[3]);

// create shift register object (number of shift registers, data pin, clock pin, latch pin)
ShiftRegister74HC595<SR_NUM_REGISTERS> sr(SR_SDI_PIN, SR_SCLK_PIN, SR_LOAD_PIN);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool command_1_sent = false;
bool command_2_sent = false;
bool command_3_sent = false;
bool command_4_sent = false;

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
const byte MAX_BANK = 4;

void setup()
{

    for (byte i = 0; i < NUM_BUTTONS; i++)
    {
        pinMode(LED_PINS[i], OUTPUT);
    }

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    delay(500);
    display.setTextSize(7);
    display.setTextColor(WHITE);
    display.clearDisplay();
    setDisplay("RDY");
    display.display();

    MIDI.begin(MIDI_CHANNEL_OMNI);

    button1.begin();
    button2.begin();
    button3.begin();
    button4.begin();

    // Only enable Serial for USB MIDI debugging
    //Serial.begin(9600);

    // Send a patch reset on start
    //MIDI.sendProgramChange(0, 1);

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
        TO_LONG_1,
        LONG_1,
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
        else if (button1.pressedFor(LONG_PRESS))
            STATE = TO_LONG_1;
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

    case TO_LONG_1:
        if (!commandMode)
        {
            setDisplay("CMD");
            callCommand(0);
        }
        else
        {
            setDisplay("RDY");
            callPreset(bankNum, 0);
        }

        if (button1.wasReleased())
            STATE = LONG_1;
        break;

    case LONG_1:
        commandMode = !commandMode;
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

    for (byte i = 0; i < NUM_BUTTONS; i++)
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
    }

    byte displayPrint[3];
    for (int i = 0; i < 3; i++)
        displayPrint[i] = getNumberToPrint(0, program)[i];
    sr.setAll(displayPrint);
}

void callPreset(byte bank, byte program)
{
    for (byte i = 0; i < NUM_BUTTONS; i++)
    {
        if ((i == program - 1) && program != 0)
            digitalWrite(LED_PINS[i], HIGH);
        else
            digitalWrite(LED_PINS[i], LOW);
    }

    msgFlicker(FLICKER_FAST, 5, getNumberToPrint(bank, program));

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

    byte displayPrint[3];
    for (int i = 0; i < 3; i++)
        displayPrint[i] = getNumberToPrint(bank, program)[i];
    sr.setAll(displayPrint);
}

void setDisplay(char *msg)
{
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(msg);
    display.display();
}

void preset_1_1()
{
    MIDI.sendProgramChange(1, 16);
    setDisplay("CLN");
}

void preset_1_2()
{
    MIDI.sendProgramChange(2, 16);
    setDisplay("CHR");
}

void preset_1_3()
{
    MIDI.sendProgramChange(3, 16);
    setDisplay("DRV");
}

void preset_1_4()
{
    MIDI.sendProgramChange(4, 16);
    setDisplay("WAH");
}

void preset_2_1()
{
}

void preset_2_2()
{
}

void preset_2_3()
{
}

void preset_2_4()
{
    MIDI.sendProgramChange(0, 16);
    setDisplay("RST");
}

void preset_3_1()
{
    MIDI.sendProgramChange(111, 16);
    MIDI.sendProgramChange(112, 16);
    MIDI.sendProgramChange(113, 16);
    MIDI.sendProgramChange(114, 16);
    MIDI.sendProgramChange(115, 16);
    MIDI.sendProgramChange(116, 16);
    MIDI.sendProgramChange(117, 16);
    MIDI.sendProgramChange(118, 16);
    setDisplay("ON");
}

void preset_3_2()
{
    MIDI.sendProgramChange(101, 16);
    MIDI.sendProgramChange(102, 16);
    MIDI.sendProgramChange(103, 16);
    MIDI.sendProgramChange(104, 16);
    MIDI.sendProgramChange(105, 16);
    MIDI.sendProgramChange(106, 16);
    MIDI.sendProgramChange(107, 16);
    MIDI.sendProgramChange(108, 16);
    setDisplay("OFF");
}

void preset_3_3()
{
}

void preset_3_4()
{
}

void preset_4_1()
{
}

void preset_4_2()
{
}

void preset_4_3()
{
}

void preset_4_4()
{
}

void preset_5_1()
{
}

void preset_5_2()
{
}

void preset_5_3()
{
}

void preset_5_4()
{
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
    if (!command_1_sent)
        MIDI.sendProgramChange(111, 16);
    else
        MIDI.sendProgramChange(101, 16);
    setDisplay("CM1");
    command_1_sent = !command_1_sent;
}

void command_2()
{
    if (!command_2_sent)
        MIDI.sendProgramChange(112, 16);
    else
        MIDI.sendProgramChange(102, 16);
    setDisplay("CM2");
    command_2_sent = !command_2_sent;
}

void command_3()
{
    if (!command_3_sent)
        MIDI.sendProgramChange(113, 16);
    else
        MIDI.sendProgramChange(103, 16);
    setDisplay("CM3");
    command_3_sent = !command_3_sent;
}

void command_4()
{
    if (!command_4_sent)
        MIDI.sendProgramChange(114, 16);
    else
        MIDI.sendProgramChange(104, 16);
    setDisplay("CM4");
    command_4_sent = !command_4_sent;
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
