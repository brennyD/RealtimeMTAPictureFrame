# MTA realtime tracker sign

<img src="https://i.imgur.com/ADWqWPq.jpeg" width="75%">

This was a project completed in the Summer of 2024 after moving to New York City. I was looking for a live piece of wall art for my apartment and took interest in [MoMA's subway circuit board](https://store.moma.org/products/traintrackr-nyc-subway-circuit-board-2), however it was too small (especially for the price) and I was a fan of the [Hertz style, geographic subway map](https://www.mta.info/map/36946) so instead I decided to make something of my own. 

### The sign in action:

https://github.com/user-attachments/assets/207011f5-2aec-4f22-8b37-4fe9cce39da8



### Materials:
* [ESP32 S3 with a matrix portal addon from Adafruit](https://www.adafruit.com/product/5778) as the driver board
* [16x32 LED panel](https://www.adafruit.com/product/420) as the light source for the stations
* [2mm clear PMMA filament](https://www.amazon.com/gp/product/B0BLH9TSHV) as a (surprising effective) light pipe
* [24x36 shadow box](https://www.hobbylobby.com/home-decor-frames/frames-framing-supplies/shadow-boxes-display-cases/jersey-display-case/p/81104055) to hold it all together
* 24x36 poster board print of a Subway map

### Software

There are 2 directories in this repo:

* MTAPI
  * A proxy server that converts the MTA's live GTFS data into a JSON format that can be fetched in a restful manner
  * This is a fork of another repo that has a few tweaks to work better with the driver board
* mta_sign
  * The arduino files that run on the ESP to pull subway data from the above server (running on a pi), polling every minute or so, where every poll can retrieve the next ~15 minutes of subway data. 
  * This could've been written a lot better, and potentially integrate with the MTA's GTFS data directly which would forgoe the need for a proxy server, however I wanted to challenge myself by writing this in C instead of an alternative like micropython. I have a raspberry pi running a few other things already so it was cheap to tack the flask server on top of it, which spared me the need to parse GTFS directly.


### Hardware setup

Under the hood the only thing the above code is doing is representing each station as a pixel on the LED matrix board. When a train is within 60 seconds of arriving or leaving a station, the pixel will light up that respective train's bullet color. If multiple trains are at a station, like what occurs often at hubs like Times Sqaure, the colors will cycle between present trains on a 1 second interval. With everything stripped away, it looks like this:



https://github.com/user-attachments/assets/8991e6cb-a24e-438c-a81f-8e20ade5f916



Excluding the Staten Island Railroad (sorry SI), most of the 512 pixels available has a station assigned to them.

From here, I 3D printed an LED shield that allows for the PMMA filament to slip right in, being held in place by friction

<img src="https://i.imgur.com/ikITR5w.jpeg" width="50%">

The clear filament serves as an excellent light guide for distances up to several feet. I chose them for this project to avoid the need for hundreds of solder connections for individual LEDs to be directly behind each station, as well as their small widths allowing lights to be cramped together in places light brooklyn with stations in close proximity to one another.

Lastly it was time to tap holes through the poster board for every station and route the filament from the LED board to each hole created:


<img src="https://i.imgur.com/C60CSag.jpeg" width="50%">

Many people are surprised to hear the subway has over 400 stations. After glueing each and every one of them, it feels like there's a lot more

<img src="https://i.imgur.com/qYN24U8.jpeg" width="50%">

Once this was done, everything was stuffed into the shadow box and the last step was to correct the station assignments to their correct pixel. Each station was routes to a pixel in no order, so I created a helper program to illuminate LEDs one at a time along with their coordinates and refactored the station definitions in code accordingly. After that, I had something you can't find at MoMA!

### What I would do differently

* Micropython for the driver code. It's a great language and I hate C
* Alternative light sources. Although filament was easy for the assembly of this project, the end result is bulkier than I would like. Doing the work to wire small SMDs or maybe a set of cheap OLED panels would've meant a much thinner end result.



