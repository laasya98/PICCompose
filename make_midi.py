#!/usr/bin/python3
import mido
from mido import Message, MetaMessage, MidiFile, MidiTrack
import serial
import usb
import subprocess
import os, sys
import serial
import time
import datetime

# TODO: make compatible with different OS

# init serial connection to board, set baud rate, time out after 25 seconds
ser = serial.Serial('/dev/cu.usbserial', 38400, timeout = 5)

# init track and midi file
mid = MidiFile()
track = MidiTrack()
mid.tracks.append(track)

# last time in microseconds since start time
last_time = 0

# are we recording
recording = True

# global tempo data
ppq = 480
bpm = 120
tempo = 60000000/bpm

# listen for the input, exit if nothing received in timeout period
while True:
   line = ser.readline(30)
   str_line = str(line).split(" ")
   print(line, len(line))
   # if statements to send messages

   # Tempo adjust (assuming pre record mode)
   if "BPM" in str_line:
      bpm = int(str_line[2])
      tempo = 60000000/bpm
      track.append(MetaMessage("set_tempo", tempo=tempo))

   # TODO: fix note timings from MCU side
   elif "START" in str_line and recording:
      # TODO: 
      diff = int(60000*(int(str_line[2]) - last_time)/(ppq*bpm))*40
      last_time = int(str_line[2])
      # print("note delay", diff)
      # want notes to be consecutive unless they're actually a rest
      if diff < 200 or diff > 10000:
         diff = 0
      track.append(Message("note_on", note=int(str_line[3]), velocity=64, time=0))

   elif "STOP" in str_line and recording:
      diff = int(60000*(int(str_line[2]) - last_time)/(ppq*bpm))*40
      last_time = int(str_line[2])
      # print("note length", diff)
      track.append(Message("note_on", note=int(str_line[3]), velocity=0, time=diff))

   # adjust record mode
   elif "BEGIN" in str_line and not recording:
      recording = True
      break  

   elif "END" in str_line and recording:
      recording = False
      break  

   elif line == b'':
      print("Time out! Saving Recording.\n")
      break

# save file with timestamp
now = datetime.datetime.now().isoformat(' ', 'seconds').replace(" ","_")
now = now.replace(":","_")
mid.save('recordings/pic_recording_' + now + '.mid')

# # open musescore
# subprocess.call(
#     ["/usr/bin/open", "-W", "-n", "-a", "/Applications/MuseScore 3.app"]
#     )



