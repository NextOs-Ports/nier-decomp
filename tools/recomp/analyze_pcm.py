#!/usr/bin/env python3
"""Measure private runtime PCM and export unnormalized front-channel WAV clips."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy.io import wavfile


def metrics(samples, rate):
    data=samples.astype(np.float64)
    rms=np.sqrt(np.mean(data*data,axis=0));peak=np.max(np.abs(data),axis=0)
    window=np.hanning(len(data))[:,None]
    power=np.mean(np.abs(np.fft.rfft(data*window,axis=0))**2,axis=1)
    freqs=np.fft.rfftfreq(len(data),1/rate)
    total=float(power.sum());prob=power/total if total else power
    nonzero=prob[prob>0]
    return dict(duration_seconds=len(data)/rate,frames=len(data),channels=data.shape[1],
        sample_rate=rate,rms_per_channel=rms.tolist(),peak_per_channel=peak.tolist(),
        rms_dbfs_per_channel=(20*np.log10(np.maximum(rms,1e-30))).tolist(),
        spectral_centroid_hz=float(np.dot(freqs,prob)),
        spectral_entropy_bits=float(-np.sum(nonzero*np.log2(nonzero))),
        bins_within_40db_of_peak=int(np.count_nonzero(power>power.max()*1e-4)) if total else 0,
        energy_80_to_12000_hz=float(power[(freqs>=80)&(freqs<=12000)].sum()/total) if total else 0)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--segment',action='append',default=[])
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    rate,source=wavfile.read(a.input,mmap=True)
    if source.dtype!=np.float32:raise ValueError('Expected unnormalized IEEE float32 runtime PCM')
    if source.ndim==1:source=source[:,None]
    front=source[:,:2]
    result=dict(input_sha256=hashlib.sha256(a.input.read_bytes()).hexdigest(),
        input_duration_seconds=len(source)/rate,input_channels=source.shape[1],
        selection='front left/right only, unchanged linear gain, no normalization',segments=[])
    for item in a.segment:
        label,start,end=item.split(':');start=float(start);end=float(end)
        if not 0<=start<end<=len(source)/rate:raise ValueError('Segment outside captured PCM')
        data=np.array(front[round(start*rate):round(end*rate)],copy=True)
        path=a.output/(label+'.wav');wavfile.write(path,rate,data)
        entry=metrics(data,rate);entry.update(name=label,start_seconds=start,end_seconds=end,
            wav=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        result['segments'].append(entry)
    (a.output/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
