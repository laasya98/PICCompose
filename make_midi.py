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
import numpy as np

# TODO: make compatible with different OS

# init serial connection to board, set baud rate, time out after 25 seconds
ser = serial.Serial('/dev/cu.usbserial', 38400, timeout = 120)

# init track and midi file
mid = MidiFile()
track = MidiTrack()
mid.tracks.append(track)

# last time in microseconds since start time
last_time = 0

# check for octave aliasing
last_start_note = 0

# are we recording
recording = False

# global tempo data
ppq = mid.ticks_per_beat
bpm = 120
tempo = int(60000000/bpm)
quarter = int(ppq)
print("Input Tempo: ")
# listen for the input, exit if nothing received in timeout period
while True:
   line = ser.readline(30)
   str_line = str(line).split(" ")
   # if statements to send messages
   # Tempo adjust (assuming pre record mode)
   if "BPM" in str_line:
      bpm = int(str_line[2])
      tempo = int(60000000/bpm)
      track.append(MetaMessage("set_tempo", tempo=tempo))
      print("TEMPO: " + str(bpm) )

   elif "START" in str_line and recording:
      # time conversion formula: ((start ms - end ms))*(1min/60000ms)(ticks/qn * qn/min)
      ms = (int(str_line[2]) - last_time)
      diff = int(ms*(ppq*bpm)/60000)
      # snap to quarter note
      diff = int(np.ceil(diff/quarter)) * quarter
      # reset the last time
      last_time = int(str_line[2])
      # get the midi note:
      note = int(str_line[3])

      # want notes to be consecutive unless they're actually a rest
      # no more than a whole rest and no less than an quarter rest
      if diff > 2000 or diff <= quarter:
         diff = 0

      # fix first note issue
      if last_start_note == 0:
         last_start_note = note

      # prevent octave aliasing
      if note > 60 and abs(note-last_start_note) < 12:
         print("START note: ", note)
         track.append(Message("note_on", note=note, velocity=64, time=diff))
         last_start_note = note

   elif "STOP" in str_line and recording:
      ms = (int(str_line[2]) - last_time)
      diff = int(ms*(ppq*bpm)/60000)
      # snap to quarter note
      if diff < quarter and diff > (5*quarter/8):
         diff = quarter
      diff = int(np.ceil(diff/quarter)) * quarter
      # reset the last time
      last_time = int(str_line[2])
      # get the midi note
      note = int(str_line[3])
      if note > 60 and abs(note-last_start_note) < 12 :
         print("STOP note: ", note)
         track.append(Message("note_on", note=note, velocity=0, time=diff))

   # adjust record mode, set record mode on if begin message received
   elif "BEGIN" in str_line and not recording:
      print("Recording Started!")
      recording = True

   # stop dumping to midi if end message sent/record mode switched off
   elif "END" in str_line and recording:
      recording = False
      print("Recording Ended! Saving...\n")
      # save file with timestamp
      now = datetime.datetime.now().isoformat(' ', 'seconds').replace(" ","_")
      now = now.replace(":","_")
      mid.save('recordings/pic_recording_' + now + '.mid')

      # open musescore
      subprocess.call(
         ["/usr/bin/open", "-W", "-n", "-a", "/Applications/MuseScore 3.app"]
         )  

      # restart process
      # init track and midi file
      mid = MidiFile()
      track = MidiTrack()
      mid.tracks.append(track)

      # reset last time in microseconds since start time
      last_time = 0
      # reset octave aliasing
      last_start_note = 0

      # reset global tempo defaults
      bpm = 120
      tempo = int(60000000/bpm)

      print("Input Tempo: ")



   elif line == b'':
      print("Time out! Saving Recording...\n")
      break





