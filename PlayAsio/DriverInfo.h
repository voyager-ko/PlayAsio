#pragma once

#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>
#include "sndfile.h"
#include "fftw3.h"
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"
using namespace std;

#define ASIO_DRIVER_NAME    "ASIO4ALL v2"

enum {
	kMaxInputChannels = 32,
	kMaxOutputChannels = 32
};


class DriverInfo
{
private:

	ASIODriverInfo driverInfo;

	long inputChannels;
	long outputChannels;

	long minSize;
	long maxSize;
	long preferredSize;
	long granularity;

	ASIOSampleRate sampleRate;

	long inputLatency;
	long outputLatency;

	long inputBuffers;
	long outputBuffers;
	ASIOBufferInfo bufferInfos[kMaxInputChannels + kMaxOutputChannels];

	ASIOChannelInfo channelInfos[kMaxInputChannels + kMaxOutputChannels];

	double nanoSeconds;
	double sample;
	double tcSamples;

	ASIOTime tInfo;
	unsigned long sysRefTime;

	bool stopped;

public:

};

