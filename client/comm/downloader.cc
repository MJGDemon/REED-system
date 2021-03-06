/*
 * downloader.cc
 */


#include "downloader.hh"

using namespace std;

/*
 * downloader thread handler
 * 
 * @param param - input param structure
 *
 */
void* Downloader::thread_handler(void* param){

	/* parse parameters*/
	param_t* tmp = (param_t*)param;
	int cloudIndex = tmp->cloudIndex;
	Downloader* obj = tmp->obj;
	free(tmp);

	/* get the download initiate signal */
	init_t signal;
	obj->signalBuffer_[cloudIndex]->Extract(&signal);

	/* get filename & name size*/
	char* filename = signal.filename;
	int namesize = signal.namesize;
	int retSize;
	int index = 0;

	/* initiate download request */
	obj->socketArray_[cloudIndex]->initDownload(filename, namesize);	
		
	/* start to download data into container */
	obj->socketArray_[cloudIndex]->downloadChunk(obj->downloadContainer_[cloudIndex], &retSize);

	/* get the header */
	shareFileHead_t* header = (shareFileHead_t*)obj->downloadContainer_[cloudIndex];
	index = sizeof(shareFileHead_t);

	/* parse the header object */
	Item_t headerObj;
	headerObj.type = 0;
	memcpy(&(headerObj.fileObj.file_header),header,sizeof(shareFileHead_t));

	/* add the header object into ringbuffer */
	obj->ringBuffer_[cloudIndex]->Insert(&headerObj,sizeof(headerObj));

	/* main loop to get data */
	while (true) {

		/* if the current comtainer has been proceed, download next container */
		if (index == retSize) {
			obj->socketArray_[cloudIndex]->downloadChunk(obj->downloadContainer_[cloudIndex],&retSize);
			index = 0;
		}
		/* get the share object */
		shareEntry_t* temp = (shareEntry_t*)(obj->downloadContainer_[cloudIndex]+index);
		int shareSize = temp->shareSize;
		index += sizeof(shareEntry_t);
		/* parse the share object */
		Item_t output;
		output.type = 1;
		memcpy(&(output.shareObj.share_header), temp, sizeof(shareEntry_t));
		memcpy(output.shareObj.data, obj->downloadContainer_[cloudIndex]+index, shareSize);
		index += shareSize;
		/* add the share object to ringbuffer */
		obj->ringBuffer_[cloudIndex]->Insert(&output,sizeof(output));
	}
	return NULL;
}

/*
 * constructor
 *
 * @param userID - ID of the user who initiate download
 * @param total - input total number of clouds
 * @param subset - input number of clouds to be chosen
 * @param obj - decoder pointer
 */
Downloader::Downloader(int total, int subset, int userID, Decoder* obj, Configuration* confObj) {

	/* set private variables */
	total_ = total;
	subset_ = subset;
	decodeObj_ = obj;

	/* initialization*/
	ringBuffer_ = (RingBuffer<Item_t>**)malloc(sizeof(RingBuffer<Item_t>*)*total_);
	signalBuffer_ = (RingBuffer<init_t>**)malloc(sizeof(RingBuffer<init_t>*)*total_);
	downloadMetaBuffer_ = (char **)malloc(sizeof(char*)*total_);
	downloadContainer_ = (char **)malloc(sizeof(char*)*total_);
	socketArray_ = (Socket**)malloc(sizeof(Socket*)*total_);
	headerArray_ = (fileShareMDHead_t **)malloc(sizeof(fileShareMDHead_t*)*total_);

	/* initialization loop  */
	for (int i = 0; i < total_; i++) {

		signalBuffer_[i] = new RingBuffer<init_t>(DOWNLOAD_RB_SIZE, true, 1);
		ringBuffer_[i] = new RingBuffer<Item_t>(DOWNLOAD_RB_SIZE, true, 1);
		downloadMetaBuffer_[i] = (char*)malloc(sizeof(char)*DOWNLOAD_BUFFER_SIZE);
		downloadContainer_[i] = (char*)malloc(sizeof(char)*DOWNLOAD_BUFFER_SIZE);

		/* create threads */
		param_t* param = (param_t*)malloc(sizeof(param_t));      // thread's parameter
		param->cloudIndex = i;
		param->obj = this;
		pthread_create(&tid_[i],0,&thread_handler, (void*)param);

		/* create sockets */
		char *IP = (char*)confObj->getServerConf(i).serverIP.c_str();
		socketArray_[i] = new Socket(IP ,confObj->getServerConf(i).dataStorePort, userID);
	}

	fileMDHeadSize_ = sizeof(fileShareMDHead_t);
	shareMDEntrySize_ = sizeof(shareMDEntry_t);
}

/*
 * destructor
 *
 */
Downloader::~Downloader(){
	int i;
	for(i = 0; i < total_; i++){
		delete(signalBuffer_[i]);
		delete(ringBuffer_[i]);
		free(downloadMetaBuffer_[i]);
		free(downloadContainer_[i]);
		delete(socketArray_[i]);
	}
	free(signalBuffer_);
	free(ringBuffer_);
	free(headerArray_);
	free(socketArray_);
	free(downloadContainer_);
	free(downloadMetaBuffer_);
}

/*
 * main procedure for downloading a file
 *
 * @param filename - targeting filename
 * @param namesize - size of filename
 * @param numOfCloud - number of clouds that we download data
 *
 */
int Downloader::downloadFile(char* filename, int namesize, int numOfCloud){

	/* temp share buffer for assemble the ring buffer data chunks*/
	unsigned char *shareBuffer;
	shareBuffer = (unsigned char*)malloc(sizeof(unsigned char)*RING_BUFFER_DATA_SIZE*MAX_NUMBER_OF_CLOUDS);

	/* add init object for download */
	init_t input;
	for (int i = 0; i < numOfCloud; i++) {

		input.type = 1;
		input.filename = filename;
		input.namesize = namesize;
		signalBuffer_[i]->Insert(&input, sizeof(init_t));
	}

	/* get the header object from buffer */
	Item_t headerObj;
	for (int i = 0; i < numOfCloud; i++) {

		ringBuffer_[i]->Extract(&headerObj);
	}

	/* parse header object, tell decoder the total number of secret */
	shareFileHead_t* header = &(headerObj.fileObj.file_header);
	int numOfShares = header->numOfShares;
	decodeObj_->setTotal(numOfShares);

	/* proceed each secret */
	int count = 0;
	while (count < numOfShares) {

		int secretSize = 0;
		int shareSize = 0;

		/* extract share object from each cloud's ringbuffer */
		for (int i = 0; i < numOfCloud; i++) {

			Item_t output;
			ringBuffer_[i]->Extract(&output);
			shareEntry_t* temp = &(output.shareObj.share_header);
			secretSize = temp->secretSize;
			shareSize = temp->shareSize;
			/* place the share at the right position */
			memcpy(shareBuffer+i*shareSize,output.shareObj.data,shareSize);
		}
		/* add the share buffer to the decoder ringbuffer */
		Decoder::ShareChunk_t package;
		package.secretID = count;
		package.secretSize = secretSize;
		package.shareSize = shareSize;
		memcpy(&(package.data), shareBuffer,numOfCloud*shareSize);
		decodeObj_->add(&package, count%DECODE_NUM_THREADS);
		count++;
	}
	free(shareBuffer);
	return 0;
}

/*
 * test if it's the end of downloading a file
 *
 */
int Downloader::indicateEnd(){

	for (int i = 0; i < total_; i++) {
		/* trying to join all threads */
		pthread_join(tid_[i],NULL);
	}
	return 1;
}

int Downloader::downloadStub(char* name, int namesize) {

	int indicator = GETSTUB;

	socketArray_[0]->genericSend((char*)&indicator, sizeof(int));
	socketArray_[0]->genericSend((char*)&namesize, sizeof(int));
	socketArray_[0]->genericSend(name, namesize);
	int length;
	socketArray_[0]->genericDownload((char*)&length, sizeof(int));
	char* stubbuffer = (char*)malloc(sizeof(char)*length);
	socketArray_[0]->genericDownload(stubbuffer, length);

	string fullName(name);
	fullName += ".stub.d";
	FILE* wp = fopen(fullName.c_str(),"w"); 
	if (wp == NULL) {

		cout<<"stub.d file can not creat"<<endl;
	}
	else {

		fwrite(stubbuffer, 1, length, wp);
		fclose(wp);
	}

	return 1;
}

//Test For Github

