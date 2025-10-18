#include <Adafruit_Protomatter.h>  // For RGB matrix
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

JsonDocument stationColorsMap;


#define HEIGHT 16                          // Matrix height (pixels) - SET TO 64 FOR 64x64 MATRIX!
#define WIDTH 32                           // Matrix width (pixels)
#define MAX_FPS 30                         // Maximum redraw rate, frames/second
#define STATION_PROXIMITY_THRESHOLD 60000  //train within 1 minute of station
#define REFRESH_INTERVAL 60000             //time between api calls
#define MAX_STATION_FILTER_SIZE 10
#define BATCH_CALL_SIZE 7

uint8_t rgbPins[] = { 42, 41, 40, 38, 39, 37 };
uint8_t addrPins[] = { 45, 36, 48, 35, 21 };
uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin = 14;

Adafruit_Protomatter matrix(
  WIDTH, 4, 1, rgbPins, 3, addrPins,
  clockPin, latchPin, oePin, false);


struct routeStatus {
  uint16_t routeColor;
  long long routeArrivalTime;
};

struct mtaStation {
  const uint8_t x;
  const uint8_t y;
  const String stationName;
  const bool isExpOrHub;
  uint8_t stationListSize;
  uint8_t renderPosition;
  SemaphoreHandle_t updateLock;                          //moved API calls to be parallel so we want to avoid race conditions when rendering
  const short numBlockedStations;                        //this is annoying but necessary it seems (without a bunch of effort to make this not stupid)
  const char* blockedStations[MAX_STATION_FILTER_SIZE];  //API clumps small stations into hubs, which is bad for rendering said stations
  routeStatus* stationRoutes;
};

long long startTimeMillis;


mtaStation stations[] = {
  { 13, 14, "A31", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //14 St
  { 27, 10, "R36", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //36 St
  { 5, 11, "129", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //28 St
  { 4, 11, "A03", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Dyckman St
  { 0, 13, "D04", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Kingsbridge Rd
  { 15, 7, "L11", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Graham Av
  { 7, 7, "214", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //West Farms Sq-E Tremont Av
  { 21, 11, "629", false, 0, 0, xSemaphoreCreateBinary(), 3, { "4", "5", "6" }, NULL },                      //Lexington Av/59 St
  { 11, 4, "216", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Freeman St
  { 20, 13, "637", false, 0, 0, xSemaphoreCreateBinary(), 4, { "B", "D", "F", "M" }, NULL },                 //Bleecker St
  { 28, 8, "B16", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //New Utrecht Av
  { 22, 4, "A50", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Rockaway Av
  { 8, 12, "122", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //79 St
  { 9, 7, "221", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                        //3 Av-149 St
  { 24, 4, "244", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Church Av
  { 20, 14, "639", true, 0, 0, xSemaphoreCreateBinary(), 3, { "6", "J", "Z" }, NULL },                       //Canal St
  { 8, 5, "213", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                        //E 180 St
  { 5, 8, "212", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Bronx Park East
  { 10, 5, "614", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Longwood Av
  { 11, 15, "R18", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //28 St
  { 9, 6, "615", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //E 149 St
  { 31, 12, "F22", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Smith-9 Sts
  { 11, 6, "R04", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //30 Av
  { 14, 8, "L08", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bedford Av
  { 16, 2, "R05", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Broadway
  { 18, 2, "710", true, 0, 0, xSemaphoreCreateBinary(), 2, {"7", "7X"}, NULL },                              //Jackson Hts-Roosevelt Av
  { 23, 8, "250", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Crown Hts-Utica Av
  { 19, 8, "A54", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Shepherd Av
  { 15, 1, "606", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Zerega Av
  { 3, 8, "207", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //219 St
  { 15, 2, "R03", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Astoria Blvd
  { 16, 7, "L14", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Morgan Av
  { 25, 0, "L29", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Canarsie-Rockaway Pkwy
  { 24, 11, "621", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //125 St
  { 2, 10, "R01", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Astoria-Ditmars Blvd
  { 28, 15, "Q03", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //72 St
  { 19, 12, "636", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Astor Pl
  { 28, 10, "R35", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //25 St
  { 21, 7, "M12", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Flushing Av
  { 28, 14, "Q04", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //86 St
  { 5, 13, "113", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //157 St
  { 25, 7, "N08", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Kings Hwy
  { 24, 3, "D26", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Prospect Park
  { 9, 13, "125", false, 0, 0, xSemaphoreCreateBinary(), 2, { "D", "A" }, NULL },                            //59 St-Columbus Circle
  { 25, 11, "624", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //103 St
  { 14, 4, "711", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //69 St
  { 7, 14, "A27", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //42 St-Port Authority Bus Terminal
  { 14, 10, "A10", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //163 St-Amsterdam Av
  { 8, 4, "205", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //233 St
  { 20, 7, "J29", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Halsey St
  { 15, 8, "L12", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Grand St
  { 25, 9, "F27", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Church Av
  { 8, 11, "A21", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //81 St-Museum of Natural History
  { 27, 14, "F14", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //2 Av
  { 19, 2, "L24", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Atlantic Av
  { 23, 3, "256", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Van Siclen Av
  { 10, 10, "302", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //145 St
  { 4, 9, "D10", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //167 St
  { 12, 10, "225", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //125 St
  { 24, 7, "D32", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue H
  { 4, 12, "A05", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //190 St
  { 14, 9, "G28", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Nassau Av
  { 17, 0, "613", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Hunts Point Av
  { 10, 6, "616", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //E 143 St-St Mary's St
  { 30, 4, "B17", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //71 St
  { 6, 12, "A15", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //125 St
  { 11, 13, "R14", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //57 St-7 Av
  { 3, 14, "108", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //207 St
  { 4, 8, "209", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Burke Av
  { 8, 3, "501", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Eastchester-Dyre Av
  { 20, 0, "G08", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Forest Hills-71 Av
  { 7, 11, "120", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //96 St
  { 28, 6, "F25", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //15 St-Prospect Park
  { 25, 10, "G32", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Myrtle-Willoughby Avs
  { 23, 4, "255", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Pennsylvania Av
  { 13, 6, "G18", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //46 St
  { 6, 7, "604", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Westchester Sq-E Tremont Av
  { 14, 12, "D17", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //34 St-Herald Sq
  { 13, 10, "A11", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //155 St
  { 29, 13, "233", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Hoyt St
  { 31, 1, "B20", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //20 Av
  { 21, 2, "L17", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Myrtle-Wyckoff Avs UNPROCESSED
  { 27, 11, "619", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //3 Av-138 St
  { 20, 1, "G09", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //67 Av
  { 24, 14, "229", false, 0, 0, xSemaphoreCreateBinary(), 7, { "2", "3", "5", "4", "6", "A", "C" }, NULL },  //Fulton St
  { 13, 7, "G20", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //36 St
  { 21, 3, "A63", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //104 St
  { 9, 9, "D08", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //174-175 Sts
  { 28, 13, "626", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //86 St
  { 27, 12, "630", true, 0, 0, xSemaphoreCreateBinary(), 3, { "4", "5", "6" }, NULL },                       //Lexington Av/53 St
  { 22, 5, "A49", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Ralph Av
  { 10, 8, "709", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //82 St-Jackson Hts
  { 0, 12, "D01", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Norwood-205 St
  { 16, 10, "633", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //28 St
  { 15, 3, "719", true, 0, 0, xSemaphoreCreateBinary(), 3, { "G", "7", "7X" }, NULL },                       //Court Sq-23 St
  { 31, 13, "F21", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Carroll St
  { 22, 7, "A47", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Kingston-Throop Avs
  { 16, 8, "L15", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Jefferson St
  { 17, 3, "B04", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //21 St-Queensbridge
  { 29, 5, "B14", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //50 St
  { 15, 5, "G29", false, 0, 0, xSemaphoreCreateBinary(), 0, {"L"}, NULL },                                   //Metropolitan Av
  { 29, 12, "A42", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Hoyt-Schermerhorn Sts
  { 10, 7, "G13", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Elmhurst Av
  { 17, 11, "630", false, 0, 0, xSemaphoreCreateBinary(), 2, { "E", "M" }, NULL },                           //51 St
  { 30, 12, "234", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Nevins St
  { 17, 13, "A36", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Chambers St
  { 28, 7, "B16", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //62 St
  { 27, 8, "A44", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Clinton-Washington Avs
  { 30, 6, "N02", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //8 Av
  { 2, 15, "101", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Van Cortlandt Park-242 St
  { 21, 0, "G10", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //63 Dr-Rego Park
  { 15, 9, "G30", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Broadway
  { 27, 6, "F26", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Fort Hamilton Pkwy UNPROCESSED
  { 30, 5, "N03", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Fort Hamilton Pkwy
  { 21, 13, "637", true, 0, 0, xSemaphoreCreateBinary(), 1, { "6" }, NULL },                                 //Broadway-Lafayette St
  { 28, 3, "N09", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue U
  { 10, 9, "G15", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //65 St
  { 16, 3, "B06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Roosevelt Island
  { 24, 0, "L28", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //East 105 St
  { 28, 12, "Q05", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //96 St
  { 29, 2, "F36", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue U
  { 31, 15, "231", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Clark St
  { 15, 11, "227", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Central Park North (110 St)
  { 13, 4, "708", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //90 St-Elmhurst Av
  { 9, 5, "218", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Intervale Av
  { 11, 12, "A22", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //72 St
  { 26, 9, "237", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Grand Army Plaza
  { 31, 0, "D43", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Coney Island-Stillwell Av
  { 3, 15, "A02", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Inwood-207 St
  { 16, 1, "G11", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Woodhaven Blvd
  { 24, 2, "242", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Sterling St
  { 28, 5, "F31", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue I
  { 17, 15, "R26", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Rector St
  { 18, 8, "J24", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Alabama Av
  { 18, 9, "A55", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Euclid Av
  { 11, 5, "220", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Jackson Av
  { 21, 8, "M13", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Lorimer St
  { 31, 7, "R41", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //59 St
  { 29, 8, "236", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bergen St
  { 30, 15, "251", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Sutter Av-Rutland Rd
  { 20, 12, "635", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //14 St-Union Sq
  { 19, 7, "A57", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Grant Av
  { 6, 8, "219", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Prospect Av
  { 13, 12, "128", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //34 St-Penn Station
  { 16, 6, "L13", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Montrose Av
  { 26, 15, "D22", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Grand St
  { 7, 5, "502", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Baychester Av
  { 11, 8, "713", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //52 St
  { 24, 13, "639", false, 0, 0, xSemaphoreCreateBinary(), 6, { "J", "N", "Q", "R", "W", "Z" }, NULL },       //Canal St
  { 31, 3, "R45", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Bay Ridge-95 St
  { 22, 14, "229", true, 0, 0, xSemaphoreCreateBinary(), 6, { "A", "C", "J", "Z", "2", "3" }, NULL },        //Fulton St
  { 13, 15, "133", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Christopher St-Sheridan Sq
  { 29, 11, "R30", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //DeKalb Av
  { 9, 4, "505", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Morris Park
  { 30, 10, "F23", false, 0, 0, xSemaphoreCreateBinary(), 2, { "F", "G" }, NULL },                           //4 Av-9 St
  { 3, 11, "104", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //231 St
  { 1, 12, "405", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bedford Park Blvd-Lehman College
  { 11, 14, "A28", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //34 St-Penn Station
  { 12, 5, "707", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Junction Blvd
  { 10, 14, "R15", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //49 St
  { 8, 8, "618", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Brook Av
  { 25, 4, "246", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Newkirk Av-Little Haiti
  { 12, 4, "611", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Elder Av
  { 17, 2, "706", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //103 St-Corona Plaza
  { 9, 3, "210", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Allerton Av
  { 29, 4, "F30", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //18 Av
  { 25, 11, "625", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //96 St
  { 4, 7, "503", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Gun Hill Rd
  { 9, 11, "A19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //96 St
  { 30, 0, "D41", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Ocean Pkwy
  { 6, 10, "116", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //125 St
  { 21, 14, "R24", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //City Hall
  { 11, 7, "G16", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Northern Blvd
  { 16, 11, "724", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //5 Av
  { 24, 15, "229", false, 0, 0, xSemaphoreCreateBinary(), 7, { "J", "Z", "4", "5", "6", "A", "C" }, NULL },  //Fulton St
  { 21, 6, "J28", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Chauncey St
  { 30, 7, "F24", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //7 Av
  { 30, 1, "N10", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //86 St
  { 16, 14, "138", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //WTC Cortlandt
  { 21, 4, "J16", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //85 St-Forest Pkwy
  { 10, 12, "124", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //66 St-Lincoln Center
  { 28, 0, "D39", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Sheepshead Bay
  { 19, 9, "A53", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Van Siclen Av
  { 25, 6, "F35", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Kings Hwy
  { 17, 1, "702", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Mets-Willets Point
  { 8, 14, "127", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Times Sq-42 St
  { 6, 15, "119", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //103 St
  { 9, 10, "301", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Harlem-148 St
  { 21, 15, "M23", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Broad St
  { 31, 4, "R44", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //86 St
  { 4, 13, "110", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //191 St
  { 26, 2, "D30", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Cortelyou Rd
  { 27, 2, "D38", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Neck Rd
  { 16, 4, "720", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Hunters Point Av
  { 22, 13, "638", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Spring St
  { 24, 10, "G31", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Flushing Av
  { 25, 5, "D31", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Newkirk Plaza
  { 26, 10, "G34", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Classon Av
  { 30, 11, "235", true, 0, 0, xSemaphoreCreateBinary(), 6, { "B", "Q", "2", "3", "4", "5" }, NULL },        //Atlantic Av-Barclays Ctr
  { 25, 13, "M19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Bowery
  { 27, 15, "F16", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //East Broadway
  { 20, 9, "J31", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Kosciuszko St
  { 27, 9, "G35", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Clinton-Washington Avs
  { 25, 8, "F29", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Ditmas Av
  { 24, 12, "627", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //77 St
  { 22, 9, "A45", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Franklin Av
  { 25, 1, "241", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //President St-Medgar Evers College
  { 27, 3, "201", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Wakefield-241 St UNPROCESSED
  { 11, 9, "714", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //46 St-Bliss St
  { 31, 8, "R40", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //53 St
  { 13, 8, "R08", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //39 Av-Dutch Kills
  { 7, 12, "121", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //86 St
  { 22, 8, "A46", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Nostrand Av
  { 31, 10, "F23", false, 0, 0, xSemaphoreCreateBinary(), 3, { "N", "R", "D" }, NULL },                      //4 Av-9 St
  { 22, 3, "A51", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Broadway Junction UNPROCESSED
  { 14, 11, "226", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //116 St
  { 23, 6, "253", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Rockaway Av
  { 29, 14, "232", false, 0, 0, xSemaphoreCreateBinary(), 4, { "5", "4", "2", "3" }, NULL },                 //Court St
  { 31, 14, "F18", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //York St
  { 18, 3, "718", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Queensboro Plaza
  { 25, 14, "639", false, 0, 0, xSemaphoreCreateBinary(), 5, { "N", "6", "Q", "R", "W" }, NULL },            //Canal St
  { 17, 7, "L19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Halsey St
  { 16, 15, "139", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Rector St
  { 5, 9, "413", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //167 St
  { 25, 15, "640", false, 0, 0, xSemaphoreCreateBinary(), 3, { "6", "5", "4" }, NULL },                      //Chambers St
  { 26, 0, "239", false, 0, 0, xSemaphoreCreateBinary(), 4, { "5", "4", "3", "N"}, NULL },                   //Botanic Garden
  { 19, 13, "R22", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Prince St
  { 28, 2, "F38", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue X
  { 23, 19, "249", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Kingston Av
  { 14, 7, "G26", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Greenpoint Av
  { 8, 9, "411", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Mt Eden Av
  { 14, 13, "R19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //23 St
  { 26, 12, "629", true, 0, 0, xSemaphoreCreateBinary(), 3, { "N", "R", "W" }, NULL },                       //59 St
  { 23, 10, "248", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Nostrand Av
  { 21, 10, "M16", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Marcy Av
  { 12, 12, "B10", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //57 St
  { 3, 10, "103", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //238 St
  { 22, 11, "623", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //110 St
  { 4, 14, "111", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //181 St
  { 19, 1, "F07", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //75 Av
  { 29, 6, "B13", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Fort Hamilton Pkwy
  { 20, 8, "J30", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Gates Av
  { 20, 2, "L25", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Sutter Av
  { 7, 13, "A25", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //50 St
  { 26, 4, "D34", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue M
  { 17, 4, "719", false, 0, 0, xSemaphoreCreateBinary(), 4, { "E", "M" , "7", "7X"}, NULL },                            //Court Sq
  { 23, 11, "622", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //116 St
  { 12, 15, "132", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //14 St
  { 5, 6, "204", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Nereid Av
  { 22, 12, "D15", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //47-50 Sts-Rockefeller Ctr
  { 3, 12, "106", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Marble Hill-225 St
  { 19, 6, "J22", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Cleveland St
  { 17, 6, "J17", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //75 St-Elderts Ln
  { 19, 4, "M09", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Knickerbocker Av
  { 27, 7, "D25", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //7 Av
  { 18, 13, "A34", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Canal St
  { 17, 5, "G29", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Lorimer St
  { 8, 15, "A30", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //23 St
  { 20, 6, "A52", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Liberty Av
  { 14, 14, "134", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Houston St
  { 27, 5, "N06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //20 Av
  { 27, 13, "L06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //1 Av
  { 18, 4, "M01", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Middle Village-Metropolitan Av
  { 26, 1, "D27", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Parkside Av
  { 28, 9, "B15", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //55 St
  { 4, 10, "109", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Dyckman St
  { 21, 5, "J21", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Norwood Av
  { 19, 5, "J15", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Woodhaven Blvd
  { 17, 14, "R25", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Cortlandt St
  { 15, 14, "136", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Franklin St
  { 13, 13, "D18", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //23 St
  { 29, 7, "B12", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //9 Av
  { 0, 11, "206", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //225 St
  { 31, 5, "R43", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //77 St
  { 20, 11, "R13", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //5 Av/59 St
  { 24, 6, "D29", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Beverley Rd
  { 18, 1, "705", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //111 St
  { 12, 3, "610", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Morrison Av-Soundview
  { 18, 15, "420", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Bowling Green
  { 10, 3, "211", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Pelham Pkwy
  { 23, 14, "229", true, 0, 0, xSemaphoreCreateBinary(), 7, { "4", "5", "6", "J", "Z", "2", "3" }, NULL },   //Fulton St
  { 30, 8, "A43", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Lafayette Av
  { 20, 5, "J19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Cypress Hills
  { 31, 6, "R42", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bay Ridge Av
  { 29, 1, "B23", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bay 50 St
  { 21, 9, "M14", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Hewes St
  { 15, 13, "A32", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //W 4 St-Wash Sq
  { 14, 15, "135", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Canal St
  { 2, 13, "409", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Burnside Av
  { 18, 6, "J20", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Crescent St
  { 15, 15, "137", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Chambers St
  { 30, 14, "232", false, 0, 0, xSemaphoreCreateBinary(), 1, { "R" }, NULL },                                //Borough Hall
  { 18, 0, "701", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Flushing-Main St
  { 31, 11, "R32", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Union St
  { 26, 8, "G33", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bedford-Nostrand Avs
  { 26, 7, "238", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Eastern Pkwy-Brooklyn Museum
  { 29, 10, "R34", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Prospect Av
  { 29, 15, "A40", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //High St
  { 17, 8, "L20", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Wilson Av
  { 12, 2, "603", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Middletown Rd
  { 14, 5, "G24", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //21 St
  { 3, 13, "107", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //215 St
  { 19, 11, "631", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Grand Central-42 St
  { 12, 14, "130", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //23 St
  { 27, 3, "B22", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //25 Av
  { 6, 6, "601", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Pelham Bay Park
  { 12, 9, "716", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //33 St-Rawson St
  { 23, 15, "230", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Wall St
  { 9, 8, "410", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //176 St
  { 22, 6, "A48", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Utica Av
  { 18, 11, "632", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //33 St
  { 4, 3, "115", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //137 St-City College UNPROCESSED
  { 13, 2, "607", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Castle Hill Av
  { 23, 7, "252", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Saratoga Av
  { 17, 9, "L21", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bushwick Av-Aberdeen St
  { 30, 9, "G36", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Fulton St
  { 16, 13, "A33", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Spring St
  { 28, 4, "F32", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bay Pkwy 30 3
  { 12, 8, "715", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //40 St-Lowery St
  { 23, 12, "B08", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Lexington Av/63 St
  { 0, 14, "D05", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Fordham Rd
  { 20, 15, "140", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Whitehall St-South Ferry
  { 24, 1, "239", false, 0, 0, xSemaphoreCreateBinary(), 2, { "S", "FS" }, NULL },                           //Botanic Garden
  { 7, 15, "726", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //34 St-Hudson Yards
  { 1, 13, "406", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Kingsbridge Rd
  { 7, 6, "504", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Pelham Pkwy
  { 14, 2, "217", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Simpson St
  { 19, 14, "228", false, 0, 0, xSemaphoreCreateBinary(), 4, { "2", "5", "4", "3"}, NULL },                  //Park Place
  { 26, 13, "L05", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //3 Av
  { 30, 2, "N07", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bay Pkwy
  { 10, 4, "608", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Parkchester
  { 18, 5, "M10", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Central Av
  { 8, 10, "D12", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //155 St
  { 13, 5, "710", true, 0, 0, xSemaphoreCreateBinary(), 4, {"R", "F", "M", "E"}, NULL },                     //74 St-Broadway
  { 7, 10, "A12", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //145 St UNPROCESSED
  { 24, 8, "F39", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Neptune Av
  { 2, 9, "222", true, 0, 0, xSemaphoreCreateBinary(), 2, { "2", "5" }, NULL },                              //149 St-Grand Concourse
  { 9, 15, "131", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //18 St
  { 27, 4, "F34", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue P
  { 2, 12, "D03", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Bedford Park Blvd
  { 4, 15, "A06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //181 St
  { 31, 2, "B19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //18 Av
  { 12, 13, "D16", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //42 St-Bryant Pk
  { 29, 3, "B18", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //79 St
  { 27, 0, "D33", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue J
  { 5, 7, "208", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Gun Hill Rd
  { 17, 12, "D19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //14 St
  { 31, 9, "R39", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //45 St ?13 6?
  { 24, 5, "D28", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Church Av
  { 19, 15, "142", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //South Ferry
  { 18, 12, "R21", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //8 St-NYU
  { 2, 14, "D07", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Tremont Av
  { 10, 11, "A20", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //86 St
  { 13, 3, "612", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Whitlock Av
  { 1, 14, "407", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Fordham Rd
  { 22, 10, "239", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Franklin Av-Medgar Evers College
  { 8, 6, "215", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //174 St
  { 11, 11, "A18", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //103 St
  { 6, 13, "117", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //116 St-Columbia University
  { 26, 5, "D35", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Kings Hwy
  { 16, 9, "L16", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //DeKalb Av
  { 12, 11, "A17", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Cathedral Pkwy (110 St)
  { 5, 12, "112", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //168 St
  { 1, 15, "D06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //182-183 Sts
  { 25, 3, "245", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Beverly Rd
  { 24, 6, "721", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Vernon Blvd-Jackson Av
  { 8, 13, "125", true, 0, 0, xSemaphoreCreateBinary(), 1, { "1" }, NULL },                                  //59 St-Columbus Circle
  { 8, 7, "617", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Cypress Av
  { 26, 6, "F33", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue N
  { 11, 10, "224", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //135 St
  { 30, 3, "B21", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Bay Pkwy
  { 21, 12, "F12", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //5 Av/53 St
  { 20, 10, "M11", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Myrtle Av
  { 14, 3, "R06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //36 Av
  { 16, 12, "634", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //23 St
  { 28, 1, "D42", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //W 8 St-NY Aquarium
  { 10, 13, "D14", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //7 Av
  { 30, 13, "F20", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Bergen St
  { 18, 10, "A59", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //80 St
  { 9, 12, "123", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //72 St
  { 22, 15, "419", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Wall St
  { 22, 0, "G12", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Grand Av-Newtown
  { 25, 2, "243", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Winthrop St
  { 5, 10, "A07", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //175 St
  { 20, 3, "M06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Seneca Av
  { 0, 15, "408", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //183 St
  { 22, 2, "A64", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //111 St
  { 23, 0, "L27", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //New Lots Av
  { 12, 6, "712", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //61 St-Woodside
  { 2, 2, "A31", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //8 Av UNPROCESSED
  { 26, 14, "F15", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //Delancey St-Essex St
  { 7, 8, "222", true, 0, 0, xSemaphoreCreateBinary(), 1, { "4" }, NULL },                                   //149 St-Grand Concourse
  { 6, 14, "118", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Cathedral Pkwy (110 St)
  { 13, 9, "G21", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //Queens Plaza
  { 11, 3, "609", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //St Lawrence Av
  { 1, 10, "416", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //138 St-Grand Concourse
  { 6, 9, "412", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //170 St
  { 12, 7, "G19", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Steinway St
  { 19, 0, "F06", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Kew Gardens-Union Tpke
  { 18, 14, "E01", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //World Trade Center
  { 28, 11, "A41", true, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Jay St-MetroTech
  { 25, 12, "628", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //68 St-Hunter College
  { 22, 1, "M05", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Forest Av
  { 23, 13, "640", true, 0, 0, xSemaphoreCreateBinary(), 2, { "J", "Z" }, NULL },                            //Brooklyn Bridge-City Hall
  { 11, 2, "602", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Buhre Av
  { 18, 7, "J23", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Van Siclen Av
  { 29, 0, "D40", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Brighton Beach
  { 23, 2, "257", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //New Lots Av
  { 7, 9, "D09", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //170 St
  { 23, 5, "254", false, 0, 0, xSemaphoreCreateBinary(), 2, {"L", "S"}, NULL },                              //Junius St
  { 23, 1, "254", false, 0, 0, xSemaphoreCreateBinary(), 1, {"3"}, NULL },                                      //Livonia Av
  { 6, 11, "A14", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //135 St
  { 2, 11, "402", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Mosholu Pkwy
  { 27, 1, "D37", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Avenue U
  { 24, 13, "N05", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //18 Av UNPROCESSED
  { 1, 11, "401", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Woodlawn
  { 3, 9, "414", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                       //161 St-Yankee Stadium
  { 13, 11, "A16", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                     //116 St
  { 29, 9, "235", true, 0, 0, xSemaphoreCreateBinary(), 3, { "D", "N", "R" }, NULL },                        //Atlantic Av-Barclays Ctr
  { 26, 3, "247", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Flatbush Av-Brooklyn College
  { 9, 14, "126", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //50 St
  { 19, 3, "M04", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL },                                      //Fresh Pond Rd
  { 5, 14, "114", false, 0, 0, xSemaphoreCreateBinary(), 0, {}, NULL }                                      //145 St
};

int totalStations = 0;


const char* WIFI_NAME = "SSID";
const char* WIFI_PSWD = "PSWD";
unsigned long refreshTime = 0;


void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_NAME, WIFI_PSWD);
  Serial.println("Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    renderLoadingEffect();
  }

  Serial.println("Connected");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.macAddress());
  Serial.println(WiFi.gatewayIP());
  Serial.println(WiFi.subnetMask());
}

void renderError() {
  Serial.print("Failed to get data");
}

void setupDisplayAndColorConfigs() {
  matrix.begin();
  uint16_t oneThroughThreeColor = matrix.color565(255, 0, 0);
  uint16_t fourThroughSixColor = matrix.color565(0, 255, 0);
  uint16_t sevenLineColor = matrix.color565(255, 0, 255);
  uint16_t yellowLineColor = matrix.color565(255, 255, 0);
  uint16_t gLineColor = matrix.color565(108, 190, 69);
  uint16_t brownLineColor = matrix.color565(44, 29, 8);
  uint16_t greyLineColor = matrix.color565(50, 50, 50);
  uint16_t blueLineColor = matrix.color565(0, 0, 255);
  uint16_t orangeLineColor = matrix.color565(255, 140, 0);
  stationColorsMap["1"] = oneThroughThreeColor;
  stationColorsMap["2"] = oneThroughThreeColor;
  stationColorsMap["3"] = oneThroughThreeColor;
  stationColorsMap["4"] = fourThroughSixColor;
  stationColorsMap["5"] = fourThroughSixColor;
  stationColorsMap["6"] = fourThroughSixColor;
  stationColorsMap["6X"] = fourThroughSixColor;
  stationColorsMap["7"] = sevenLineColor;
  stationColorsMap["7X"] = sevenLineColor;
  stationColorsMap["Q"] = yellowLineColor;
  stationColorsMap["N"] = yellowLineColor;
  stationColorsMap["R"] = yellowLineColor;
  stationColorsMap["W"] = yellowLineColor;
  stationColorsMap["G"] = gLineColor;
  stationColorsMap["J"] = brownLineColor;
  stationColorsMap["Z"] = brownLineColor;
  stationColorsMap["S"] = greyLineColor;
  stationColorsMap["SS"] = greyLineColor;
  stationColorsMap["L"] = greyLineColor;
  stationColorsMap["A"] = blueLineColor;
  stationColorsMap["C"] = blueLineColor;
  stationColorsMap["E"] = blueLineColor;
  stationColorsMap["B"] = orangeLineColor;
  stationColorsMap["D"] = orangeLineColor;
  stationColorsMap["F"] = orangeLineColor;
  stationColorsMap["FS"] = orangeLineColor;
  stationColorsMap["FX"] = orangeLineColor;
  stationColorsMap["M"] = orangeLineColor;
}



void renderLoadingEffect() {
  for (int x = 0; x < matrix.width(); x++) {
    for (int y = 0; y < matrix.height(); y++) {
      matrix.drawPixel(x, y, matrix.color565(random(255), random(255), random(255)));
    }
  }
  matrix.show();
  matrix.fillScreen(matrix.color565(0, 0, 0));
}

int arraySize;
routeStatus* routesForStation;


void updateMatrixBufferWithStation(mtaStation* stationToRender) {
  uint16_t colorToRender;

  if (stationToRender->stationListSize != 0) {
    if (isTrainAtStation(stationToRender->stationRoutes[stationToRender->renderPosition].routeArrivalTime)) {
      colorToRender = stationToRender->stationRoutes[stationToRender->renderPosition % stationToRender->stationListSize].routeColor;
      stationToRender->renderPosition++;
    } else {
      uint8_t nextArrivingTrain = 0;
      while (nextArrivingTrain < stationToRender->stationListSize) {
        if (!isTrainArrivingEventually(stationToRender->stationRoutes[nextArrivingTrain].routeArrivalTime)) {
          nextArrivingTrain++;
        } else {
          break;
        }
      }
      if (nextArrivingTrain >= stationToRender->stationListSize) {
        colorToRender = renderDefaultStation(*stationToRender);
        stationToRender->renderPosition = 0;
      } else {
        if (!isTrainAtStation(stationToRender->stationRoutes[nextArrivingTrain].routeArrivalTime)) {
          colorToRender = renderDefaultStation(*stationToRender);
          stationToRender->renderPosition = nextArrivingTrain;  //We will sit on this index until train arrives
        } else {
          colorToRender = stationToRender->stationRoutes[nextArrivingTrain].routeColor;  //Cycle back to beginning since arriving trains exist
          stationToRender->renderPosition = nextArrivingTrain + 1;                       //this will wrap
        }
      }
    }
  } else {
    colorToRender = renderDefaultStation(*stationToRender);  //if NO trains are even listed as arriving then there's definitely nothing to render
  }

  matrix.drawPixel(stationToRender->x, stationToRender->y, colorToRender);
}

void cycleStationColors(mtaStation stationsToRender[], int stationListSize) {

  for (int i = 0; i < stationListSize; i++) {
    if (xSemaphoreTake(stationsToRender[i].updateLock, (10 * portTICK_PERIOD_MS))) {  //10ms to aqcuire lock, else skip
      updateMatrixBufferWithStation(&stationsToRender[i]);
      xSemaphoreGive(stationsToRender[i].updateLock);
    } else {
      Serial.print("No semaphore for rendering :( at station: ");
      Serial.println(stationsToRender[i].stationName);
    }
  }
  matrix.show();
  delay(1000);
}

uint16_t renderDefaultStation(mtaStation station) {
  return matrix.color565(0, 0, 0);  //Render nothing if not
}

bool isTrainArrivingEventually(long long trainArrivalTime) {
  return trainArrivalTime - getCurrentTimeMillis() > 0;
}

bool isTrainAtStation(long long trainArrivalTime) {
  long long timeDelta = trainArrivalTime - getCurrentTimeMillis();
  return isTrainArrivingEventually(trainArrivalTime) && timeDelta < STATION_PROXIMITY_THRESHOLD;
}

long long getCurrentTimeMillis() {
  return startTimeMillis + (long long)millis();
}

void dataUpdateLoop(void* params) {
  updateAllStationData(stations, totalStations);
  refreshTime = millis();
  while (1) {
    if (millis() - refreshTime > REFRESH_INTERVAL || refreshTime == 0) {
      Serial.println("Refreshing station data");
      int currentIndex = 0;
      while (currentIndex + BATCH_CALL_SIZE < totalStations) {
        batchUpdate(stations, currentIndex, currentIndex + BATCH_CALL_SIZE);
        currentIndex += BATCH_CALL_SIZE;
        delay(1000);  //I know this is lazy but this will make batch updating looks smoother
      }
      batchUpdate(stations, currentIndex, totalStations);
      refreshTime = millis();
    }
    delay(1000);
  }
}

void setup(void) {
  Serial.begin(9600);
  setupDisplayAndColorConfigs();
  setupWifi();
  startTimeMillis = getRealTime();
  matrix.fillScreen(matrix.color565(0, 0, 0));
  totalStations = sizeof(stations) / sizeof(stations[0]);
  for (int i = 0; i < totalStations; i++) {
    xSemaphoreGive(stations[i].updateLock);
  }

  xTaskCreate(
    dataUpdateLoop,    // Function to implement the task
    "dataUpdateLoop",  // Name of the task
    3000,              // Stack size in bytes
    NULL,              // Task input parameter
    0,                 // Priority of the task
    NULL               // Task handle.
  );
}

void loop(void) {
  if (refreshTime != 0) {
    cycleStationColors(stations, totalStations);
  } else {
    renderLoadingEffect();
  }
}
