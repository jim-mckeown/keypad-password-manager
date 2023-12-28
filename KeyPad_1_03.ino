/******************************************************************
*  
*  Send password associated with each key to USB keyboard
*  Unit powers up in mode 0, locked, sends key character to USB keyboard
*  Enter #PIN# to unlock and enter mode 1, unlocked 
*  Use a serial terminal such as putty to enter mode 2 (editPW)
*  Enter mode 3 (RESET) - erase all passwords, set new PIN, lock device
*  
*  Version 1.02
*  Reduce serial baud rate from 57600 to 9600
*  
*  Version 1.03
*  Ubuntu + X11, no random shift with 10-20 mS delay between characters
*  Ubuntu + Wayland still has issue with random shift
*  keyboardDelay set to 15 mS   
*    
******************************************************************/

#include <Keyboard.h>
#include <EEPROM.h>
#include <Keypad.h>

String rev = "1.03";
String releaseDate = "28 December 2023";
String author = "Jim McKeown";
int eeStrOffset = 63; // size of passwords in bytes, including null terminator
int keyboardDelay = 15; // mS delay between keyboard characters for Ubuntu
int pwLength = EEPROM.length() / eeStrOffset; // maximum number of passwords
int maxPin = EEPROM.length() % eeStrOffset; // maximum PIN length, including null terminator
String eePin = ""; 
String pin = "";

uint8_t mode = 0; // mode 0: locked (default on power-up)
                  // mode 1: send stored text string (after unlock code entered)
                  // mode 2: edit passwords ('editPW' from USB serial port)
                  // mode 3: erase all passwords and set new PIN ('RESET' from USB serial port)

int8_t id = 0; // password index

// Keypad setup
const byte ROWS = 4; 
const byte COLS = 4; 

char hexaKeys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {3, 4, 5, 6}; 
byte colPins[COLS] = {7, 8, 9, 2}; 

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

void setup()
{
  Keyboard.begin(); // setup usb keyboard
  Serial.begin(9600); // setup usb serial port
  eePin = eeReadString(eeStrOffset * pwLength); // read PIN from eeprom 
  customKeypad.setDebounceTime(100); // change debounce from default 50 ms
  delay(1000); // wait for serial port to start
}

void loop()
{
  if(mode == 0 || mode == 1) // normal button to password mode
  {
    char customKey = customKeypad.getKey();
    if (customKey)
    {
      if(mode == 0)
      {
        // just send keypad character to keyboard if device is locked
        Keyboard.print(customKey); // send keypad value to keyboard
        if(customKey == '#')
        {
          // check what has accumulated in pin variable.
          // if it matches the eePin read from eeprom, unlock the device.
          if(pin.equals(eePin))
          {
            mode = 1; // unlock device
          }
          else
          {
            pin = ""; // clear accumulated key strokes
          }  
        }
        // for any key other than '#', accumulate the character in pin variable.
        else
        {
          pin = pin + customKey;
        }
      }
      else
      {
        // mode = 1, unlocked so send password for this key to USB keyboard
        int8_t keyNum = getKeyIndex(customKey);
        String textOut = eeReadString(keyNum * eeStrOffset);
        for(int i = 0;i < textOut.length();i++)
        {
          Keyboard.print(textOut.substring(i, i + 1));
          delay(keyboardDelay);
        }
      } 
    }
  }
  
  if(mode == 2) // edit passwords
  {
    Serial.println("Ready to edit password");
    Serial.println("Type the key to associate with this password, then press Enter...");
    Serial.println("Or type \'q\' and press Enter to quit and lock device.");
    char strId = readStr()[0]; // get the key character
    if(strId == 'q')
    {
      Serial.println("Locking device...");
      mode = 0;
    }
    else
    {
      id = getKeyIndex((char) strId); // convert the character to eeprom index
      if (id >= 0 && id < pwLength) // check eeprom index for valid range
      {
        Serial.print("Editing password for key ");
        Serial.println(strId);
        Serial.print("Enter password for key "); 
        Serial.print(strId); 
        Serial.print(" max "); 
        Serial.print(eeStrOffset - 1); 
        Serial.println(" characters:");
        String pw = readStr();
        Serial.print("Password = "); Serial.println(pw);
        int eeAddress = id * eeStrOffset;
        eeWriteString(eeAddress, pw);
      }
      else
      {
        Serial.print(strId);
        Serial.println(" is not a valid key for this device.");
        Serial.println("Valid keys are: 0,1,2,3,4,5,6,7,8,9,A,B,C,D,*,#");
      }

    }
  }

  if(mode == 3) // erase passwords and set new PIN
  {
    Serial.println("WARNING! YOU ARE ABOUT TO ERASE ALL DATA ON THIS DEVICE!");
    Serial.println("Are you sure you want to continue?");
    String strCmd = readStr();
    if(strCmd.equalsIgnoreCase("yes"))
    {
      // erase eeprom
      Serial.println("Erasing all data...");
      for(int i = 0;i < EEPROM.length();i++)
      {
        EEPROM.write(i, 0);
      }

      // write place holder passwords in each eeprom slot
      for(int i = 0;i < ROWS;i++)
      {
        for(int j = 0;j < COLS;j++)
        {
          String clearPw = "Password ";
          clearPw.concat(hexaKeys[i][j]);
          eeWriteString((i * COLS * eeStrOffset) + (j * eeStrOffset), clearPw);
          Serial.print("Key "); Serial.print(hexaKeys[i][j]); Serial.println(" set to: " + clearPw);
        }
      }
      Serial.println("Data erased");
      
      // prompt for new PIN
      Serial.print("PIN must be "); Serial.print(maxPin - 1); Serial.print(" characters or less.");
      Serial.print("Use only the characters on the device keypad (case sensitive).");
      Serial.println("Do NOT use \'#\'");
      Serial.println("Enter new PIN:");
      String pin = readStr();
      
      // write PIN to remaining space after last password slot in eeprom
      int eeAddress = eeStrOffset * pwLength;
      eeWriteString(eeAddress, pin);
      
      // read back PIN from eeprom
      eePin = eeReadString(eeStrOffset * pwLength);
      Serial.println("PIN set to: " + eePin); 
    }
    // set mode to zero (locked)
    mode = 0;
    Serial.println("Device is locked.");
  }

  // check serial port for text commands
  if(Serial.available() != 0) // do not block is nothing in buffer
  {
    String strCmd = readStr();
    if(strCmd.equals("editPW"))
    {
      mode = 2;
    }
    if(strCmd.equals("RESET"))
    {
      mode = 3;
    }   
    if(strCmd.equals("?"))
    {
      about();
    }
  }
} 

/***************************************************************
 * Send information on firmware, fingerprint sensor, eeprom
 * to serial port.
 * 
 **************************************************************/
void about()
{
  Serial.println("Password Keypad");
  Serial.print("Version "); Serial.println(rev); 
  Serial.print("Release date: "); Serial.println(releaseDate); 
  Serial.print("Author: "); Serial.println(author);    
  Serial.print("Maximum password length = "); Serial.println(eeStrOffset - 1);
  Serial.print("Maximum number of passwords = "); Serial.println(pwLength);
  Serial.print("Maximim PIN length: "); Serial.println(maxPin - 1);   
  Serial.println("Enter 'editPW' to edit passwords.");
  Serial.println("Enter 'RESET' erase all passwords and reset PIN.");
  Serial.println("Enter '? to see this information with no mode change.");
}

// wait for serial port to be available
// strip off whitespace including cr, lf
String readStr(void) 
{
  while (! Serial.available());
  String strRead = Serial.readString();
  // strip off whitespace including cr, lf
  strRead.trim();
  return strRead;
}

void eeWriteString(int eeAddr, String strWrite)
{
  int strLen = strWrite.length() + 1;
  char charArray[strLen];
  strWrite.toCharArray(charArray, strLen);
  for(int i = 0;i < strLen;i++)
  {
    EEPROM.write(eeAddr + i, charArray[i]);
  }
}

String eeReadString(int eeAddr)
{
  char charArray[eeStrOffset];
  for(int i = 0;i < eeStrOffset;i++)
  {
    charArray[i] = EEPROM.read(eeAddr + i);
    if(charArray[i] == 0)
    {
      break;
    }
  }
  return String(charArray);
}

int8_t getKeyIndex(char key)
{
  int8_t index = -1;
  for(int i = 0;i < ROWS;i++)
  {
    for(int j = 0;j < COLS;j++)
    {
      if(hexaKeys[i][j] == key)
      {
        index = (i * ROWS) + j;
      }
    }
  }
  return index;
}
