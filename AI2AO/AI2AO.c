#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "NIDAQmx.h"

// XXX: Assume 
// 1. R_BUF_LEN % N_SAMPLES == 0 --> 如果不是會有資料遺漏 (WCallback 實作)!
// 2. 

/* Define BOS1901 */
#define BOS1901_ON 0

#define DAQmxErrChk(functionCall) if( DAQmxFailed(error=(functionCall)) ) goto Error; else

/* variable hangle */
static TaskHandle  AItaskHandle = 0, AOtaskHandle = 0;


/* function declaration */
static int32 GetTerminalNameWithDevPrefix(TaskHandle taskHandle, const char terminalName[], char triggerName[]);

int32 CVICALLBACK R_EveryNCallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void* callbackData);

int32 CVICALLBACK W_DoneCallback(TaskHandle taskHandle, int32 status, void* callbackData);

/* Number define */
#define R_SAMPLING_RATE 10000.0
#define R_PER_CH 10000 /* Read samples per channel */

#if BOS1901_ON == 0
#define R_CH_NUM  2
#else 
#define R_CH_NUM  1
#endif

#define R_BUF_LEN (R_PER_CH*R_CH_NUM) /* Buffen len */

#define R_CH_ORIGIN "Dev2/ai6" /* original signal */
#define R_CH_TEST   "Dev2/ai0" /* test channel's name. Test only, will be deleted*/
#define W_CH        "Dev2/ao0" /* write channel's name */

#define N_SAMPLES 20 /* read call back len and write len */

/* Struct define */
/* Make a struct for R_Callback */
typedef struct RCallbackInfo {
	float64* const data;
	const float64** p2_r_ptr;        /* pointer point to r_ptr */
	int32* round_ptr;
}RCallbackInfo;

/* Read Only */
typedef struct WCallbackInfo {
	const float64* const data;
	const float64** const p2_r_ptr;
	const float64**  p2_w_ptr;      /* should modify by write callback */
	int32* round_ptr;
}WCallbackInfo;

int main()
{
	int32   error = 0;
	char    errBuff[2048] = { '\0' };
	char    trigName[256];

	float64  AIdata[R_BUF_LEN] = { 0 }; // maybe use malloc ?
	float64* w_ptr = AIdata;
	float64* r_ptr = AIdata;
	int32    round = 0;

	/* Input Task Config */
	DAQmxErrChk(DAQmxCreateTask("", &AItaskHandle));
	DAQmxErrChk(DAQmxCreateAIVoltageChan(AItaskHandle, R_CH_ORIGIN, "", DAQmx_Val_Diff, -10.0, 10.0, DAQmx_Val_Volts, NULL));

#ifdef BOS1901_ON == 0
	/* Enable ai0, or this signal will go to BOS1901 */
	DAQmxErrChk(DAQmxCreateAIVoltageChan(AItaskHandle, R_CH_TEST, "", DAQmx_Val_Diff, -10.0, 10.0, DAQmx_Val_Volts, NULL));
#endif

	DAQmxErrChk(DAQmxCfgSampClkTiming(AItaskHandle, "", R_SAMPLING_RATE, DAQmx_Val_Rising, DAQmx_Val_ContSamps, R_PER_CH));
	DAQmxErrChk(GetTerminalNameWithDevPrefix(AItaskHandle, "ai/StartTrigger", trigName));
	DAQmxErrChk(DAQmxSetBufInputBufSize(AItaskHandle, R_BUF_LEN * 2)); // TODO: check this

	/* Output Task Config */
	DAQmxErrChk(DAQmxCreateTask("", &AOtaskHandle));
	DAQmxErrChk(DAQmxCreateAOVoltageChan(AOtaskHandle, W_CH, "", -10.0, 10.0, DAQmx_Val_Volts, NULL));

	/* Log File Config */
	DAQmxErrChk(DAQmxConfigureLogging(AItaskHandle, "D:\\data\\AI2AO.tdms", DAQmx_Val_LogAndRead, "AIs", DAQmx_Val_OpenOrCreate));

	/* TODO: Verify 讀和寫 per channel 數量不一樣可不可以? */
	/* TODO: WHY 兩個的 CLK 不一樣 */
	DAQmxErrChk(DAQmxCfgSampClkTiming(AOtaskHandle, "", R_SAMPLING_RATE / 2, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, N_SAMPLES));

	struct RCallbackInfo r_info = {
	   .data = AIdata,
	   .p2_r_ptr = &r_ptr,
	   .round_ptr = &round
	};

	struct WCallbackInfo w_info = {
	   .data = AIdata,
	   .p2_r_ptr = &r_ptr,
	   .p2_w_ptr = &w_ptr,
	   .round_ptr = &round
	};

	struct RCallbackInfo* r_info_ptr = &r_info;
	struct RCallbackInfo* w_info_ptr = &w_info;

	/* 讀更新 r_ptr , 寫更新 w_ptr */
	DAQmxErrChk(DAQmxRegisterEveryNSamplesEvent(AItaskHandle, DAQmx_Val_Acquired_Into_Buffer, N_SAMPLES, 0, R_EveryNCallback, r_info_ptr));
	DAQmxErrChk(DAQmxRegisterDoneEvent(AOtaskHandle, 0, W_DoneCallback, w_info_ptr)); // TODO: open new thread avoid stuck ?

	/* Preload Default Value to Write RAM Buf */
	DAQmxErrChk(DAQmxWriteAnalogF64(AOtaskHandle, N_SAMPLES, FALSE, 10.0, DAQmx_Val_GroupByChannel, AIdata, NULL, NULL));

	
    /*********************************************/
	// DAQmx Start Code
	/*********************************************/
	DAQmxErrChk(DAQmxStartTask(AItaskHandle));
	DAQmxErrChk(DAQmxStartTask(AOtaskHandle));

	printf("Acquiring samples continuously. Press Enter to interrupt\n");
	printf("\nRead:\tAI\tTotal:\tAI\tround\n");
	getchar();


Error:
	if (DAQmxFailed(error))
		DAQmxGetExtendedErrorInfo(errBuff, 2048);
	if (AItaskHandle) {
		/*********************************************/
		// DAQmx Stop Code
		/*********************************************/
		DAQmxStopTask(AItaskHandle);
		DAQmxClearTask(AItaskHandle);
		AItaskHandle = 0;
	}
	if (AOtaskHandle) {
		/*********************************************/
		// DAQmx Stop Code
		/*********************************************/
		DAQmxStopTask(AOtaskHandle);
		DAQmxClearTask(AOtaskHandle);
		AOtaskHandle = 0;
	}
	if (DAQmxFailed(error))
		printf("DAQmx Error: %s\n", errBuff);
	printf("End of program, press Enter key to quit\n");
	getchar();
	return 0;
}

static int32 GetTerminalNameWithDevPrefix(TaskHandle taskHandle, const char terminalName[], char triggerName[])
{
	int32	error = 0;
	char	device[256];
	int32	productCategory;
	uInt32	numDevices, i = 1;

	DAQmxErrChk(DAQmxGetTaskNumDevices(taskHandle, &numDevices));
	while (i <= numDevices) {
		DAQmxErrChk(DAQmxGetNthTaskDevice(taskHandle, i++, device, 256));
		DAQmxErrChk(DAQmxGetDevProductCategory(device, &productCategory));
		if (productCategory != DAQmx_Val_CSeriesModule && productCategory != DAQmx_Val_SCXIModule) {
			*triggerName++ = '/';

#pragma warning (disable: 4996)
			strcat(strcat(strcpy(triggerName, device), "/"), terminalName);
			break;
		}
	}

Error:
	return error;
}

// Callbacks

/* 只要 ch 第一個, data_layout ch1..., ch2... */
/* TODO: 以後可以有動態長度，讓太慢的時候少進一點 wcallback */
int32 CVICALLBACK W_DoneCallback(TaskHandle taskHandle, int32 status, void* callbackData)
{
	int32   error = 0;
	char    errBuff[2048] = { '\0' };

	// Check to see if an error stopped the task.
	DAQmxErrChk(status);

	WCallbackInfo* info = (WCallbackInfo*)callbackData;

	while ((* (info->p2_r_ptr) - *(info->p2_w_ptr) + R_BUF_LEN * *(info->round_ptr)) <= 0) {/* spin lock if r_ptr lead w_ptr < N_SAMPLES*/ }

	// https://forums.ni.com/t5/Measurement-Studio-for-VC/DAQmxWriteAnalogF64-slow-in-communication/td-p/3280633
	 DAQmxErrChk(DAQmxWriteAnalogF64(AOtaskHandle, N_SAMPLES, FALSE, 10.0, DAQmx_Val_GroupByChannel, *(info->p2_w_ptr), NULL, NULL));

	/* Move w_ptr */
	bool BufOverFlow = ((*(info->p2_r_ptr) + R_CH_NUM * N_SAMPLES - info->data) >= R_BUF_LEN);
	if (BufOverFlow) {
		*(info->p2_w_ptr) = info->data;
		*(info->round_ptr) -= 1;
	} else { *(info->p2_w_ptr) += R_CH_NUM * N_SAMPLES; }

Error:
	if (DAQmxFailed(error)) {
		DAQmxGetExtendedErrorInfo(errBuff, 2048);
		DAQmxClearTask(taskHandle);
		if (AItaskHandle) {
			DAQmxStopTask(AItaskHandle);
			DAQmxClearTask(AItaskHandle);
			AItaskHandle = 0;
		}
		if (AOtaskHandle) {
			DAQmxStopTask(AOtaskHandle);
			DAQmxClearTask(AOtaskHandle);
			AOtaskHandle = 0;
		}
		printf("DAQmx Error: %s\n", errBuff);
	}
	return 0;
}

int32 CVICALLBACK R_EveryNCallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void* callbackData)
{
	char        errBuff[2048] = { '\0' };
	int32       error = 0;
	uInt64      readAI = 0;

	static  uInt64    totalAI = 0;


	RCallbackInfo* info = (RCallbackInfo*)callbackData;

	/*********************************************/
	// DAQmx Read Code
	/*********************************************/

	// Read safe: check r_ptr + all channels' samples aren't overflow
	uInt32 next_r_ptr_idx = *(info->p2_r_ptr) + R_CH_NUM * N_SAMPLES - info->data;
	bool UnProperBufOverFlow = next_r_ptr_idx > R_BUF_LEN;

	if (UnProperBufOverFlow) {
		*(info->p2_r_ptr) = info->data;
		*(info->round_ptr) += 1;
		printf("ReadBufNotAlign -> ReadOverFlow, realign to head\n");
		fflush(stdout);
	}

	DAQmxErrChk(DAQmxReadAnalogF64(AItaskHandle, N_SAMPLES, 10.0, DAQmx_Val_GroupByChannel, *(info->p2_r_ptr), N_SAMPLES * R_CH_NUM, &readAI, NULL));

	/* update r_ptr, TODO: NOTICE THAT IF WE CHANGE TO readAI * R_CH_NUM --> Good space usage but MAY CAUSE DATA UNALIGN TO N_SAMPLES LAYOUT.
	   Then, you should change W_DoneCallback also */
	bool ProperBufOverFlow = next_r_ptr_idx == R_BUF_LEN; // go back head
	if (ProperBufOverFlow) {
		*(info->p2_r_ptr) = info->data;
		*(info->round_ptr) += 1;
	}else { *(info->p2_r_ptr) += N_SAMPLES * R_CH_NUM; }

	printf("\t%d\t\t%d\t%5d\r", (int)readAI, (int)(totalAI += readAI), *(info->round_ptr));
	fflush(stdout);

Error:
	if (DAQmxFailed(error)) {
		DAQmxGetExtendedErrorInfo(errBuff, 2048);
		/*********************************************/
		// DAQmx Stop Code
		/*********************************************/
		if (AItaskHandle) {
			DAQmxStopTask(AItaskHandle);
			DAQmxClearTask(AItaskHandle);
			AItaskHandle = 0;
		}
		if (AOtaskHandle) {
			DAQmxStopTask(AOtaskHandle);
			DAQmxClearTask(AOtaskHandle);
			AOtaskHandle = 0;
		}
		printf("DAQmx Error: %s\n", errBuff);
	}
	return 0;
}


