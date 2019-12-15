#!/usr/bin/python3

import mido
import numpy as np 
from mido import MidiFile

mid = MidiFile('midi_test.mid')

for i, track in enumerate(mid.tracks):
    print('Track {}: {}'.format(i, track.name))
    for msg in track:
        print(msg)

