
const String MTA_SERVER = "http://192.168.1.69:5000";
const String BY_STATION_ROUTE = "/by-id/";
const String TIME_ROUTE = "/time";


#define BATCH_CALL_SIZE 50

const String timeKey = "time";
const String routeKey = "route";



long long getRealTime() {
  HTTPClient http;
  http.setTimeout(10000);
  bool result = http.begin((MTA_SERVER + TIME_ROUTE).c_str());
  long long time = 0;
  if (!result) {
    renderError();
    return time;
  }
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    JsonDocument timeData;
    deserializeJson(timeData, http.getString());
    time = timeData["time"];
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    renderError();
  }
  // Free resources
  http.end();
  Serial.print("Systime is: ");
  Serial.println(time);
  return time;
}


void clearAllStationData(mtaStation stations[], int listSize) {
  Serial.println("Clearing Station Data");
  for (int i = 0; i < listSize; i++) {
    free(stations[i].stationRoutes);
    stations[i].stationListSize = 0;
    stations[i].renderPosition = 0;
  }
  Serial.println("Cleared Station Data");
}

void updateAllStationData(mtaStation stations[], int listSize) {
  Serial.println("Updating Station Data");
  int currentIndex = 0;
  while(currentIndex + BATCH_CALL_SIZE < listSize) {
    batchUpdate(stations, currentIndex, currentIndex + BATCH_CALL_SIZE);
    currentIndex+=BATCH_CALL_SIZE;
  }
  batchUpdate(stations, currentIndex, listSize);
}

void batchUpdate(mtaStation stations[], int startIndex, int endIndex) {
  String urlPathList = "";
  for (int i = startIndex; i < endIndex; i++) {
    urlPathList += stations[i].stationName + ",";
  }
  urlPathList.remove(urlPathList.length() - 1);
  Serial.print("Start Index: ");
  Serial.print(startIndex);
  Serial.print(" End Index: ");
  Serial.println(endIndex);
  Serial.print("Making Batch call with list: ");
  Serial.println(urlPathList);
  String results = retrieveStationData(urlPathList);
  if (results.equals("")) {
    return;  //if this keeps failing local data will be used until map is blank
  }
  JsonDocument parsedStation;
  deserializeJson(parsedStation, results);
  Serial.println("Parsing Station Data");
  JsonArray updates = parsedStation["data"];
  for (int i = startIndex; i < endIndex; i++) {
    JsonObject currentStation = updates[i - startIndex];
    JsonArray northBound = currentStation["N"].as<JsonArray>();
    JsonArray southBound = currentStation["S"].as<JsonArray>();
    Serial.println("Data parsed, preparaing to filter");
    filterOutBadStations(stations[i], northBound);
    filterOutBadStations(stations[i], southBound);
    if(xSemaphoreTake(stations[i].updateLock, (200 * portTICK_PERIOD_MS)) ) { //1s max to aqcuire lock
      free(stations[i].stationRoutes);
      stations[i].stationRoutes = NULL;
      stations[i].stationListSize = 0;
      stations[i].renderPosition = 0;
      stations[i].stationRoutes = mergeSortDirectionalResults(northBound, southBound, &stations[i].stationListSize);
      xSemaphoreGive(stations[i].updateLock);
    } else {
      Serial.println("Couldn't get semaphore for UI");
    }
  }
}

void filterOutBadStations(mtaStation station, JsonArray routes) {
  for(int i = 0; i<station.numBlockedStations; i++) {
    for(int j = 0; j<routes.size(); j++) { //filter size is const so this is technically O(n) don't yell at me I'm tired I hate C!
      if(strcmp((const char*)routes[j]["route"],(const char*)station.blockedStations[i]) == 0) {
        routes.remove(j);
        j--;
      }
    }
  }
}

String retrieveStationData(String stations) {
  HTTPClient http;
  String payload = "";
  Serial.print("Calling URL: ");
  Serial.println((MTA_SERVER + BY_STATION_ROUTE + stations).c_str());
  bool result = http.begin((MTA_SERVER + BY_STATION_ROUTE + stations).c_str());
  if (!result) {
    renderError();
  }
  int httpResponseCode = http.GET();


  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    renderError();
  }
  http.end();
  return payload;
}

routeStatus* mergeSortDirectionalResults(JsonArray northBoundTrains, JsonArray southBoundTrains, uint8_t* arraySize) {
  *arraySize = southBoundTrains.size() + northBoundTrains.size();
  // Serial.println(*arraySize);
  routeStatus* mergeSortedArrivals = (routeStatus*)malloc(*arraySize * sizeof(routeStatus));
  uint8_t southPointer = 0;
  uint8_t northPointer = 0;
  uint8_t insertionPointer = 0;
  long long northTime = 0;
  long long southTime = 0;
  while (northPointer < northBoundTrains.size() || southPointer < southBoundTrains.size()) {
    /* uncomment for debugging
    Serial.println((const char*)northBoundTrains[northPointer]["route"]);
    Serial.println((long long)northBoundTrains[northPointer]["time"]);
    Serial.println((const char*)southBoundTrains[southPointer]["route"]);
    Serial.println((long long)southBoundTrains[southPointer]["time"]);
    */
    if (northPointer >= northBoundTrains.size()) {
      mergeSortedArrivals[insertionPointer++] = convertJsonVarToRoute(southBoundTrains[southPointer++]);
    } else if (southPointer >= southBoundTrains.size()) {
      mergeSortedArrivals[insertionPointer++] = convertJsonVarToRoute(northBoundTrains[northPointer++]);
    } else {
      northTime = northBoundTrains[northPointer][timeKey];
      southTime = southBoundTrains[southPointer][timeKey];
      if (northTime < southTime) {
        mergeSortedArrivals[insertionPointer++] = convertJsonVarToRoute(northBoundTrains[northPointer++]);
      } else {
        mergeSortedArrivals[insertionPointer++] = convertJsonVarToRoute(southBoundTrains[southPointer++]);
      }
    }
  }
  return mergeSortedArrivals;
}


routeStatus convertJsonVarToRoute(JsonObject route) {
  routeStatus convertedRoute = { stationColorsMap[(const char*)route[routeKey]], (long long)route[timeKey] };
  return convertedRoute;
}