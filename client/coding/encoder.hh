/*
 *  encoder.hh 
 */

#ifndef __ENCODER_HH__
#define __ENCODER_HH__

#include "BasicRingBuffer.hh"
#include "CryptoPrimitive.hh"
#include "uploader.hh"
#include "aont.hh"
#include "conf.hh"

/* num of encoder threads */
#define NUM_THREADS 1

/* ringbuffer size */
#define RB_SIZE (1024)

#define KEY_SIZE 32

/* max secret size */
#define SECRET_SIZE (16*1024)

/* max share buffer size */
#define SHARE_BUFFER_SIZE (4*16*1024)

#define STUB_BUFFER_SIZE (1024*1024*1024)

#define STUB_SIZE 64

#define KEY_BATCH_SIZE 256

/* object type indicators */
#define FILE_OBJECT 1
#define FILE_HEADER (-9)
#define SHARE_OBJECT (-8)
#define SHARE_END (-27)

using namespace std;

class Encoder{
	public:
		/* threads parameter structure */
		typedef struct{
			int index;    // thread number
			Encoder* obj; // encoder object pointer
		}param_encoder;

		/* file head structure */
		typedef struct{
			unsigned char data[SECRET_SIZE];
			int fullNameSize;
			int fileSize;
		}fileHead_t;

		/* secret metadata structure */
		typedef struct{
			unsigned char data[SECRET_SIZE];
			unsigned char key[KEY_SIZE];
			int secretID;
			int secretSize;
			int end;
		}Secret_t;

		/* share metadata structure */
		typedef struct{
			unsigned char data[SHARE_BUFFER_SIZE];
			int secretID;
			int secretSize;
			int shareSize;
			int end;
		}ShareChunk_t;

		/* union header for secret ringbuffer */
		typedef struct{
			union{
				Secret_t secret;
				fileHead_t file_header;
			};
			int type;
		}Secret_Item_t;

		/* union header for share ringbuffer */
		typedef struct{
			union{
				ShareChunk_t share_chunk;
				fileHead_t file_header;
			};
			int type;
		}ShareChunk_Item_t;

		/* the input secret ringbuffer */
		RingBuffer<Secret_Item_t>** inputbuffer_;

		/* the output share ringbuffer */
		RingBuffer<ShareChunk_Item_t>** outputbuffer_;

		/* thread id array */
		pthread_t tid_[NUM_THREADS+1];

		/* the total number of clouds */
		int n_;

		int totalChunk_;

		unsigned char* stubBuffer_;

		/* index for sequencially adding object */
		int nextAddIndex_;

		/* uploader object */
		Uploader* uploadObj_;

		/* crypto object array */
		CryptoPrimitive** cryptoObj_;

		Aont* aontObj_[NUM_THREADS];

		/*
		 * constructor of encoder
		 *
		 * @param n - total number of shares generated from a secret
		 * @param securetype - encryption and hash type
		 * @param uploaderObj - pointer link to uploader object
		 *
		 *
		 */
		Encoder(int n, 
				int securetype, 
				Uploader* uploaderObj);

		/*
		 * destructor of encoder
		 */
		~Encoder();

		/*
		 * test if it's end of encoding a file
		 */
		void indicateEnd();

		void setTotalChunk(int n);

		/*
		 * add function for sequencially add items to each encode buffer
		 *
		 * @param item - input object
		 */
		int add(Secret_Item_t* item);

		/*
		 * thread handler for encoding secret into shares
		 *
		 * @param param - parameters for each thread
		 */
		static void* thread_handler(void* param);

		/*
		 * collect thread for getting share objects in order
		 *
		 * @param param - parameters for collect thread
		 */
		static void* collect(void* param);

		void encStub(char* path, char* key);
};

#endif
