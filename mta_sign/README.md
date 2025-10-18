# Sign driver files

### mta_sign.ino

* Persists a mapping of MTA stations which contains:
   * Their respective station codes
   * Which trains they are allow to render (some stations contain overlapped train data based on what the api returns and may be duplicates)
* Drives the matrix panel to poll station data and update the LED matrix in a thread safe manner, in batches as to make the station updates more fluid

### mta_server.ino
* Polls the MTAPI for updates station data and transforms the results to be used by the matrix driver

### stopIdentifier.ino
* Helper program that slowly cycles through every pixel on the LED matrix, simply illuminating a pixel and printing it's x,y coordinates
  * This was useful in mapping pixels to the stations they were glued to after the sign was assembled