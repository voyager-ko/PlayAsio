#if 0 echo
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
int test = 0;

// Make a file
ofstream outFile;
uint32_t totalDataWritten = 0;

const uint32_t sampleRate = 44100;
const uint16_t bitsPerSamples = 16;
const uint16_t channels = 1;

enum {
	kMaxInputChannels = 32,
	kMaxOutputChannels = 32
};

// 全体を管理する構造体
typedef struct DriverInfo {
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
}DriverInfo; // エイリアスの影響　宣言するときに、structと書く必要がなくなる

// 全メンバを初期化してインスタンスを作る
DriverInfo asioDriverInfo = { 0 };
ASIOCallbacks asioCallbacks;

extern AsioDrivers* asioDrivers; // 外部のファイルでも参照できるようにする　staticはそのファイルだけで有効

bool loadAsioDriver(char* name);
long init_asio_static_data(DriverInfo* asioDriverInfo);
ASIOError create_asio_buffers(DriverInfo* asioDriverInfo);
unsigned long get_sys_reference_time();


// callback prototypes
void bufferSwitch(long index, ASIOBool processNow);
ASIOTime* bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool processNow);
void sampleRateChanged(ASIOSampleRate sRate);
long asioMessages(long selector, long value, void* message, double* opt);


void init_file(ofstream& file, uint32_t sampleRate, uint16_t bitsPerSamples, uint16_t channels, uint32_t dataSize) {

	file.write("RIFF", 4);
	// RIff と ChunkSize以外のサイズ
	uint32_t chunkSize = 36 + dataSize; // 仮のサイズ
	file.write(reinterpret_cast<const char*>(&chunkSize), 4);
	file.write("WAVE", 4);

	file.write("fmt ", 4);
	uint32_t subchunkSize = 16;
	uint16_t audioFormat = 1; // PCM
	file.write(reinterpret_cast<const char*>(&subchunkSize), 4);
	file.write(reinterpret_cast<const char*>(&audioFormat), 2);
	file.write(reinterpret_cast<const char*>(&channels), 2);
	file.write(reinterpret_cast<const char*>(&sampleRate), 4);

	uint32_t byteRate = sampleRate * channels * bitsPerSamples / 8;
	file.write(reinterpret_cast<const char*>(&byteRate), 4);

	uint16_t blockAlign = channels * bitsPerSamples / 8;
	file.write(reinterpret_cast<const char*>(&blockAlign), 2);

	file.write(reinterpret_cast<const char*>(&bitsPerSamples), 2);

	file.write("data", 4);
	file.write(reinterpret_cast<const char*>(&dataSize), 4); // 仮のサイズ

}

// 最後のデータサイズ調整
void finalize_wav_header(ofstream& file, uint32_t totalDataWritten) {
	uint32_t dataSize = totalDataWritten;
	uint32_t chunkSize = 36 + dataSize;

	file.seekp(4, ios::beg);
	file.write(reinterpret_cast<const char*>(&chunkSize), 4);

	file.seekp(40, ios::beg);
	file.write(reinterpret_cast<const char*>(&dataSize), 4);
}


long init_asio_static_data(DriverInfo* asioDriverInfo) {

	// Driverの情報を集める

// チャンネル情報を集める
	if (ASIOGetChannels(&asioDriverInfo->inputChannels, &asioDriverInfo->outputChannels) == ASE_OK) {

		cout << "inputChannels : " << asioDriverInfo->inputChannels << "outputChannels : "
			<< asioDriverInfo->outputChannels << endl;

	}

	cout << endl;

	// buffer情報を集める
	if (ASIOGetBufferSize(&asioDriverInfo->minSize, &asioDriverInfo->maxSize,
		&asioDriverInfo->preferredSize, &asioDriverInfo->granularity) == ASE_OK) {

		cout << "bufferSizeの情報" << endl;
		cout << "minSize : " << asioDriverInfo->minSize << " maxSize : " << asioDriverInfo->maxSize
			<< " preferredSize : " << asioDriverInfo->preferredSize
			<< " granularity : " << asioDriverInfo->granularity << endl;
	}

	cout << endl;
	// sampleRate情報を集める

	// 現在のsampleRateを知る
	if (ASIOGetSampleRate(&asioDriverInfo->sampleRate) == ASE_OK) {
		cout << "sampleRate : " << asioDriverInfo->sampleRate << endl;

		if (asioDriverInfo->sampleRate <= 0.0 || asioDriverInfo->sampleRate > 96000.0)
		{

			// sampleRateを設定する
			if (ASIOSetSampleRate(44100.0) == ASE_OK) {

				// sampleRateの確認を知る
				if (ASIOGetSampleRate(&asioDriverInfo->sampleRate) == ASE_OK) {
					cout << "sampleRate : " << asioDriverInfo->sampleRate << endl;
				}
				else {
					return 6;
				}

			}
			else {
				return 5;
			}
		}
	}
	return 0;
}


ASIOTime* bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool processNow) {
	long buffSize = asioDriverInfo.preferredSize;
	int inputSize = 1;
	int outputSize = 1;

	//モノラル 
	int32_t* inputBuffer = nullptr;
	int32_t* outputBuffer = nullptr;

	////ステレオ
	//vector<int32_t*> inputBuffers = { nullptr };
	//vector<int32_t*> outputBuffers = { nullptr };

	// モノラル
	for (int i = 0; i < inputSize + outputSize; i++) {
		if (asioDriverInfo.bufferInfos[i].isInput == ASIOTrue) {
			inputBuffer = static_cast<int32_t*>(asioDriverInfo.bufferInfos[i].buffers[index]);
		}
		else {
			outputBuffer = static_cast<int32_t*>(asioDriverInfo.bufferInfos[i].buffers[index]);
		}
	}


	// ステレオ
	/*for (int i = 0; i < inputSize + outputSize; i++) {
		if (asioDriverInfo.bufferInfos[i].isInput == ASIOTrue) {
			inputBuffers.push_back(static_cast<int32_t*>(asioDriverInfo.bufferInfos[i].buffers[index]));
		}
		else {
			outputBuffers.push_back(static_cast<int32_t*>(asioDriverInfo.bufferInfos[i].buffers[index]));
		}
	}*/

	const float feedback = 0.5f;
	const float delayTimeSec = 0.5f;
	const int delaySamples = static_cast<int>(sampleRate * delayTimeSec);
	static vector<float> delayBuffer(delaySamples, 0.0f);
	static int delayIndex = 0;

	// エコー処理と出力

	//if (inputBuffers[0] && inputBuffers[1] && outputBuffers[0] && outputBuffers[1]) {

	//	for (int j = 0; j < buffSize; j++) {
	//		for (int ch = 0; ch < outputSize; ch++) {
	//			int32_t rawSample = inputBuffers[ch][j];
	//			float input = rawSample / static_cast<float>(0x7FFFFFFF); // 最大値で正規化

	//			float delayed = delayBuffer[delayIndex];
	//			float output = input + delayed * feedback;

	//			delayBuffer[delayIndex] = output;
	//			delayIndex = (delayIndex + 1) % delaySamples;

	//			float clamped = clamp(output, -1.0f, 1.0f);
	//			int32_t out = static_cast<int32_t>(clamped * 0x7FFFFFFF); // 元の値の戻す
	//			outputBuffers[ch][j] = out;

	//			int16_t sample16 = static_cast<int16_t>(clamped * 32767.0f);
	//			outFile.write(reinterpret_cast<const char*>(&sample16), sizeof(int16_t));
	//			totalDataWritten += sizeof(int16_t);
	//		}
	//	}
	//}

		if (inputBuffer && outputBuffer) {

			for (int j = 0; j < buffSize; j++) {
				int32_t rawSample = inputBuffer[j];
				float input = rawSample / static_cast<float>(0x7FFFFFFF);

				float delayed = delayBuffer[delayIndex];
				float output = input + delayed * feedback;

				delayBuffer[delayIndex] = output;
				delayIndex = (delayIndex + 1) % delaySamples;
	 
				float clamped = clamp(output, -1.0f, 1.0f);
				int32_t outSample = static_cast<int32_t>(clamped * 0x7FFFFFFF);
				outputBuffer[j] = outSample;

				int16_t sample16 = static_cast<int16_t>(clamped * 32767.0f);
				outFile.write(reinterpret_cast<const char*>(&sample16), sizeof(int16_t));
				totalDataWritten += sizeof(int16_t);
			}
		}

	// 音声流してよいよの連絡
	ASIOOutputReady();
	return timeInfo;
}






void bufferSwitch(long index, ASIOBool processNow) {

	ASIOTime  timeInfo;
	memset(&timeInfo, 0, sizeof(timeInfo));


	// get the time stamp of the buffer, not necessary if no
	// synchronization to other media is required
	if (ASIOGetSamplePosition(&timeInfo.timeInfo.samplePosition, &timeInfo.timeInfo.systemTime) == ASE_OK)
		timeInfo.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;

	bufferSwitchTimeInfo(&timeInfo, index, processNow);
	return;

}
void sampleRateChanged(ASIOSampleRate sRate) {

}
long asioMessages(long selector, long value, void* message, double* opt) {
	long ret = 0;
	return ret;
}

ASIOError create_asio_buffers(DriverInfo* asioDriverInfo) {
	long i;
	ASIOError result;

	ASIOBufferInfo* info = asioDriverInfo->bufferInfos;

	if (asioDriverInfo->inputChannels > kMaxInputChannels) {
		asioDriverInfo->inputBuffers = kMaxInputChannels;
	}
	else {
		asioDriverInfo->inputBuffers = asioDriverInfo->inputChannels;
	}
	
	for (i = 0; i < channels * 2; i++, info++) {
		if (i == 0) {
			info->isInput = ASIOTrue;
			info->channelNum = i; // bufferInfos[i]に対応
			info->buffers[0] = info->buffers[1] = 0; 
		}
		else {
			info->isInput = ASIOFalse;
			info->channelNum = i;
			info->buffers[0] = info->buffers[1] = 0;
		}
	}

	//outputの準備
	if (asioDriverInfo->outputChannels > kMaxOutputChannels) {
		asioDriverInfo->outputBuffers = kMaxOutputChannels;
	}
	else {
		asioDriverInfo->outputBuffers = asioDriverInfo->outputChannels;
	}


	// bufferを作る
	result = ASIOCreateBuffers(asioDriverInfo->bufferInfos,
		asioDriverInfo->inputBuffers + asioDriverInfo->outputBuffers,
		asioDriverInfo->preferredSize, &asioCallbacks);

	if (result == ASE_OK)
	{
		for (i = 0; i < asioDriverInfo->inputBuffers + asioDriverInfo->outputBuffers; i++)
		{

			// 移している　bufferInfo →　channelInfo
			asioDriverInfo->channelInfos[i].channel = asioDriverInfo->bufferInfos[i].channelNum;
			asioDriverInfo->channelInfos[i].isInput = asioDriverInfo->bufferInfos[i].isInput;
			result = ASIOGetChannelInfo(&asioDriverInfo->channelInfos[i]);
			if (result != ASE_OK) {
				break;
			}
			else {
				cout << "i : " << i << "Name :" << asioDriverInfo->channelInfos[i].name << endl; 
			}
		}

		if (result == ASE_OK)
		{
			result = ASIOGetLatencies(&asioDriverInfo->inputLatency, &asioDriverInfo->outputLatency);
			if (result == ASE_OK)
				printf("ASIOGetLatencies (input: %d, output: %d);\n", asioDriverInfo->inputLatency, asioDriverInfo->outputLatency);
		}
	}
	return result;
}


int main()
{
	asioDriverInfo.stopped = false;
	cout << __cplusplus << endl;

	// ファイルオープンとヘッダー作成
	outFile.open("Asio.wav", ios::binary);
	init_file(outFile, sampleRate, bitsPerSamples, channels, 0); // dataSizeは仮

	if (loadAsioDriver(ASIO_DRIVER_NAME)) {
		if (ASIOInit(&asioDriverInfo.driverInfo) == ASE_OK) {
			if (init_asio_static_data(&asioDriverInfo) == 0) {
				asioCallbacks.bufferSwitch = &bufferSwitch;
				asioCallbacks.sampleRateDidChange = &sampleRateChanged;
				asioCallbacks.asioMessage = &asioMessages;
				asioCallbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;

				if (create_asio_buffers(&asioDriverInfo) == ASE_OK) {
					if (ASIOStart() == ASE_OK) {
						cout << "Recording... Press Enter to stop" << endl;

						cin.get(); // 録音停止はEnterキー

						asioDriverInfo.stopped = true;

						ASIOStop();
						finalize_wav_header(outFile, totalDataWritten);
						outFile.close();
						cout << "Finished recording." << endl;
					}

					ASIODisposeBuffers();
				}
			}

			ASIOExit();
		}

		asioDrivers->removeCurrentDriver();
	}

	return 0;

}
#endif

#if 0 手の音の検出
#include <iostream>
#include <sndfile.h>
#include <fftw3.h>
#include <vector>
using namespace std;

int main() {
	
	int f_low = 300;
	int f_high = 1500;

	
	const char* name = "audio/Hello.wav";
	SF_INFO info;
	SNDFILE* infile = sf_open(name, SFM_READ, &info);

	if (!infile) {
		cerr << "can't read file" << endl;
		return 0;
	}
	vector<double> buffer(info.frames * info.channels);
	sf_read_double(infile, buffer.data(), info.frames);
	int x_size = buffer.size();

	fftw_complex* in, * out;
	fftw_plan plan;
	in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * x_size);
	out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * x_size);
	plan = fftw_plan_dft_1d(x_size, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

	for (int i = 0; i < x_size; i++) {
		in[i][0] = buffer[i];
	}

	fftw_execute(plan);


	int fs = info.samplerate;


	int k_low = f_low * fs / x_size;
	int k_high = f_high * fs / x_size;
	cout << "k_low" << k_low << endl;
	cout << "k_high" << k_high << endl;


	double SumEnergy = 0.0;

	for (int i = k_low; i <= k_high; i++) {
		SumEnergy += out[i][0] * out[i][0] + out[i][1] * out[i][1];
	}

	cout << SumEnergy << endl;
	double ok = 100000;
	if (SumEnergy < ok) {
		cout << "手をたたく音です" << endl;
	}
	else {
		cout << "人の声です" << endl;
	}

	fftw_destroy_plan(plan);
	fftw_free(in);
	fftw_free(out);

	return 0;
}
#endif

#if 1 音の検出
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
int test = 0;

// Make a file
ofstream outFile;
uint32_t totalDataWritten = 0;

const uint32_t sampleRate = 44100;
const uint16_t bitsPerSamples = 16;
const uint16_t channels = 1;

enum {
	kMaxInputChannels = 32,
	kMaxOutputChannels = 32
};

// 全体を管理する構造体
typedef struct DriverInfo {
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
}DriverInfo; // エイリアスの影響　宣言するときに、structと書く必要がなくなる

// 全メンバを初期化してインスタンスを作る
DriverInfo asioDriverInfo = { 0 };
ASIOCallbacks asioCallbacks;

extern AsioDrivers* asioDrivers; // 外部のファイルでも参照できるようにする　staticはそのファイルだけで有効

bool loadAsioDriver(char* name);
long init_asio_static_data(DriverInfo* asioDriverInfo);
ASIOError create_asio_buffers(DriverInfo* asioDriverInfo);
unsigned long get_sys_reference_time();


// callback prototypes
void bufferSwitch(long index, ASIOBool processNow);
ASIOTime* bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool processNow);
void sampleRateChanged(ASIOSampleRate sRate);
long asioMessages(long selector, long value, void* message, double* opt);


void init_file(ofstream& file, uint32_t sampleRate, uint16_t bitsPerSamples, uint16_t channels, uint32_t dataSize) {

	file.write("RIFF", 4);
	// RIff と ChunkSize以外のサイズ
	uint32_t chunkSize = 36 + dataSize; // 仮のサイズ
	file.write(reinterpret_cast<const char*>(&chunkSize), 4);
	file.write("WAVE", 4);

	file.write("fmt ", 4);
	uint32_t subchunkSize = 16;
	uint16_t audioFormat = 1; // PCM
	file.write(reinterpret_cast<const char*>(&subchunkSize), 4);
	file.write(reinterpret_cast<const char*>(&audioFormat), 2);
	file.write(reinterpret_cast<const char*>(&channels), 2);
	file.write(reinterpret_cast<const char*>(&sampleRate), 4);

	uint32_t byteRate = sampleRate * channels * bitsPerSamples / 8;
	file.write(reinterpret_cast<const char*>(&byteRate), 4);

	uint16_t blockAlign = channels * bitsPerSamples / 8;
	file.write(reinterpret_cast<const char*>(&blockAlign), 2);

	file.write(reinterpret_cast<const char*>(&bitsPerSamples), 2);

	file.write("data", 4);
	file.write(reinterpret_cast<const char*>(&dataSize), 4); // 仮のサイズ

}

// 最後のデータサイズ調整
void finalize_wav_header(ofstream& file, uint32_t totalDataWritten) {
	uint32_t dataSize = totalDataWritten;
	uint32_t chunkSize = 36 + dataSize;

	file.seekp(4, ios::beg);
	file.write(reinterpret_cast<const char*>(&chunkSize), 4);

	file.seekp(40, ios::beg);
	file.write(reinterpret_cast<const char*>(&dataSize), 4);
}


long init_asio_static_data(DriverInfo* asioDriverInfo) {

	// Driverの情報を集める

// チャンネル情報を集める
	if (ASIOGetChannels(&asioDriverInfo->inputChannels, &asioDriverInfo->outputChannels) == ASE_OK) {

		cout << "inputChannels : " << asioDriverInfo->inputChannels << "outputChannels : "
			<< asioDriverInfo->outputChannels << endl;

	}

	cout << endl;

	// buffer情報を集める
	if (ASIOGetBufferSize(&asioDriverInfo->minSize, &asioDriverInfo->maxSize,
		&asioDriverInfo->preferredSize, &asioDriverInfo->granularity) == ASE_OK) {

		cout << "bufferSizeの情報" << endl;
		cout << "minSize : " << asioDriverInfo->minSize << " maxSize : " << asioDriverInfo->maxSize
			<< " preferredSize : " << asioDriverInfo->preferredSize
			<< " granularity : " << asioDriverInfo->granularity << endl;
	}

	cout << endl;
	// sampleRate情報を集める

	// 現在のsampleRateを知る
	if (ASIOGetSampleRate(&asioDriverInfo->sampleRate) == ASE_OK) {
		cout << "sampleRate : " << asioDriverInfo->sampleRate << endl;

		if (asioDriverInfo->sampleRate <= 0.0 || asioDriverInfo->sampleRate > 96000.0)
		{

			// sampleRateを設定する
			if (ASIOSetSampleRate(44100.0) == ASE_OK) {

				// sampleRateの確認を知る
				if (ASIOGetSampleRate(&asioDriverInfo->sampleRate) == ASE_OK) {
					cout << "sampleRate : " << asioDriverInfo->sampleRate << endl;
				}
				else {
					return 6;
				}

			}
			else {
				return 5;
			}
		}
	}
	return 0;
}


ASIOTime* bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool processNow) {
	long buffSize = asioDriverInfo.preferredSize;
	int inputSize = 1;
	int outputSize = 1;

	//モノラル 
	int32_t* inputBuffer = nullptr;

	// モノラル
	for (int i = 0; i < inputSize; i++) {
		if (asioDriverInfo.bufferInfos[i].isInput == ASIOTrue) {
			inputBuffer = static_cast<int32_t*>(asioDriverInfo.bufferInfos[i].buffers[index]);
		}
	}



	// 音声流してよいよの連絡
	ASIOOutputReady();
	return timeInfo;
}






void bufferSwitch(long index, ASIOBool processNow) {

	ASIOTime  timeInfo;
	memset(&timeInfo, 0, sizeof(timeInfo));


	// get the time stamp of the buffer, not necessary if no
	// synchronization to other media is required
	if (ASIOGetSamplePosition(&timeInfo.timeInfo.samplePosition, &timeInfo.timeInfo.systemTime) == ASE_OK)
		timeInfo.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;

	bufferSwitchTimeInfo(&timeInfo, index, processNow);
	return;

}
void sampleRateChanged(ASIOSampleRate sRate) {

}
long asioMessages(long selector, long value, void* message, double* opt) {
	long ret = 0;
	return ret;
}

ASIOError create_asio_buffers(DriverInfo* asioDriverInfo) {
	long i;
	ASIOError result;

	ASIOBufferInfo* info = asioDriverInfo->bufferInfos;

	if (asioDriverInfo->inputChannels > kMaxInputChannels) {
		asioDriverInfo->inputBuffers = kMaxInputChannels;
	}
	else {
		asioDriverInfo->inputBuffers = asioDriverInfo->inputChannels;
	}

	for (i = 0; i < channels * 2; i++, info++) {
		if (i == 0) {
			info->isInput = ASIOTrue;
			info->channelNum = i; // bufferInfos[i]に対応
			info->buffers[0] = info->buffers[1] = 0;
		}
		else {
			info->isInput = ASIOFalse;
			info->channelNum = i;
			info->buffers[0] = info->buffers[1] = 0;
		}
	}

	//outputの準備
	if (asioDriverInfo->outputChannels > kMaxOutputChannels) {
		asioDriverInfo->outputBuffers = kMaxOutputChannels;
	}
	else {
		asioDriverInfo->outputBuffers = asioDriverInfo->outputChannels;
	}


	// bufferを作る
	result = ASIOCreateBuffers(asioDriverInfo->bufferInfos,
		asioDriverInfo->inputBuffers + asioDriverInfo->outputBuffers,
		asioDriverInfo->preferredSize, &asioCallbacks);

	if (result == ASE_OK)
	{
		for (i = 0; i < asioDriverInfo->inputBuffers + asioDriverInfo->outputBuffers; i++)
		{

			// 移している　bufferInfo →　channelInfo
			asioDriverInfo->channelInfos[i].channel = asioDriverInfo->bufferInfos[i].channelNum;
			asioDriverInfo->channelInfos[i].isInput = asioDriverInfo->bufferInfos[i].isInput;
			result = ASIOGetChannelInfo(&asioDriverInfo->channelInfos[i]);
			if (result != ASE_OK) {
				break;
			}
			else {
				cout << "i : " << i << "Name :" << asioDriverInfo->channelInfos[i].name << endl;
			}
		}

		if (result == ASE_OK)
		{
			result = ASIOGetLatencies(&asioDriverInfo->inputLatency, &asioDriverInfo->outputLatency);
			if (result == ASE_OK)
				printf("ASIOGetLatencies (input: %d, output: %d);\n", asioDriverInfo->inputLatency, asioDriverInfo->outputLatency);
		}
	}
	return result;
}


int main()
{
	asioDriverInfo.stopped = false;
	cout << __cplusplus << endl;

	// ファイルオープンとヘッダー作成
	outFile.open("Asio.wav", ios::binary);
	init_file(outFile, sampleRate, bitsPerSamples, channels, 0); // dataSizeは仮

	if (loadAsioDriver(ASIO_DRIVER_NAME)) {
		if (ASIOInit(&asioDriverInfo.driverInfo) == ASE_OK) {
			if (init_asio_static_data(&asioDriverInfo) == 0) {
				asioCallbacks.bufferSwitch = &bufferSwitch;
				asioCallbacks.sampleRateDidChange = &sampleRateChanged;
				asioCallbacks.asioMessage = &asioMessages;
				asioCallbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;

				if (create_asio_buffers(&asioDriverInfo) == ASE_OK) {
					if (ASIOStart() == ASE_OK) {
						cout << "Recording... Press Enter to stop" << endl;

						cin.get(); // 録音停止はEnterキー

						asioDriverInfo.stopped = true;

						ASIOStop();
						finalize_wav_header(outFile, totalDataWritten);
						outFile.close();
						cout << "Finished recording." << endl;
					}

					ASIODisposeBuffers();
				}
			}

			ASIOExit();
		}

		asioDrivers->removeCurrentDriver();
	}

	return 0;

}

#endif

