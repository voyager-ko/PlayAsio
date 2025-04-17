#pragma once

#include <DriverInfo.h>
class FileController
{
private:
public:
	void init_file(ofstream& file, uint32_t sampleRate, uint16_t bitsPerSamples, uint16_t channels, uint32_t dataSize);
	void finalize_wav_header(ofstream& file, uint32_t totalDataWritten);
};

