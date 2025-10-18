import csv



position = -1
WIDTH = 32
HEIGHT = 16


with open('stations.csv') as stationFile:
    reader = csv.reader(stationFile)
    for row in reader:
        x = position % WIDTH
        y = int((position / WIDTH) % HEIGHT)
        print("{{{},{}, \"{}\", false, 0, 0, xSemaphoreCreateBinary(), 0,{{}}, NULL}}, //{} UNPROCESSED".format(x,y, row[4], row[1]))
        position+=1
    print(position)