#/usr/bin/env python

from numpy import *
from scipy import *
from pylab import *

from scipy.io import wavfile

fs, data = wavfile.read("qarb_output.wav")

ddata = diff(data)
nonzero_idxs = find(ddata)
print all(ddata[nonzero_idxs] == ddata[nonzero_idxs[0]])

print float_(10000*ddata[nonzero_idxs])

plot(ddata[nonzero_idxs])
show()