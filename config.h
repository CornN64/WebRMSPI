// SSID and password of Wifi connection:
const char* ssid = "MYSSID";
const char* password = "MYPW";

// Configure IP addresses of the local access point
const IPAddress local_IP(192,168,55,33);
const IPAddress gateway(192,168,55,1);
const IPAddress subnet(255,255,255,0);

#define USEAP 0	//Use (0)DHCP or (1)Access Point
//#define OTAenable //uncomment if you want OTA updates enabled (not sure how stable this is?!)
 
//Pin definitions using the VSPI bus (also connect GND & VCC(3.3V) to RM3100)
#define SPISPD 1000000  //SPI clock speed
#define DRDY_GPIO 35 //Data Ready Pin
#define CS_GPIO 5
#define CLK_GPIO 18
#define MISO_GPIO 19
#define MOSI_GPIO 23

//Options (setup to send data every AVG seconds)
#define AVG 60          //Average N samples and send data to the client every 60s (faster interval -> noiser data)
#define SR 36           //sample rate (depends on RM3100 config, trim this to get close to 1s)
#define ARRAY_LENGTH 1440 //How much data to store (eats stack)
#define CYCLECOUNT 800  //default = 200
#define singleMode 0    //0 = use continuous measurement mode; 1 = use single measurement mode
#define useDRDYPin 1    //0 = not using DRDYPin ; 1 = using DRDYPin to wait for data
