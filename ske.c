#include "ske.h"
#include "prf.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* memcpy */
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#ifdef LINUX
#define MMAP_SEQ MAP_PRIVATE|MAP_POPULATE
#else
#define MMAP_SEQ MAP_PRIVATE
#endif

/* NOTE: since we use counter mode, we don't need padding, as the
 * ciphertext length will be the same as that of the plaintext.
 * Here's the message format we'll use for the ciphertext:
 * +------------+--------------------+-------------------------------+
 * | 16 byte IV | C = AES(plaintext) | HMAC(C) (32 bytes for SHA256) |
 * +------------+--------------------+-------------------------------+
 * */

/* we'll use hmac with sha256, which produces 32 byte output */
#define HM_LEN 32
#define KDF_KEY "qVHqkOVJLb7EolR9dsAMVwH1hRCYVx#I"
/* need to make sure KDF is orthogonal to other hash functions, like
 * the one used in the KDF, so we use hmac with a key. */

/** Generates HMAC and AES KEY
 * Hash KDF_KEY with SHA-512 to get 64 bytes of key
 * and split in half for two 32 bytes key, one for 
 * HMAC and other half for AES
 * 
 * precondition: 
 * K = SKE_KEY object containing HMAC and AES Key
 * entropy = random
 * entLen = length of entropy
 * 
 * postcondition:
 * K's HMAC and AES Key gets updated
 */

int ske_keyGen(SKE_KEY* K, unsigned char* entropy, size_t entLen)
{
	/* TODO: write this.  If entropy is given, apply a KDF to it to get
	 * the keys (something like HMAC-SHA512 with KDF_KEY will work).
	 * If entropy is null, just get a random key (you can use the PRF). */

	// Variable for temporary key storage of length KLEN_SKE
	// Note KLEN_SKE is 32
	size_t KLEN_2X = KLEN_SKE*2;
	unsigned char tempKey[KLEN_2X];//size 64

	// If entropy is given apply KDF - HMACSHA512 elseif is null randBytes for random key
	if(entropy)
	{
		/* Computes the MAC of the entLen bytes at entropy using hash
		 * function EVP_sha512 and the key, KDF_KEY which is HM_LEN 
		 * bytes long
		 * 
		 * Output goes in tempKey and size in NULL
		 */
		HMAC(EVP_sha512(),KDF_KEY,HM_LEN,entropy,entLen,
				tempKey,NULL);
	}
	else
	{
		/* Random key of KLEN_SKE length
		 *
		 * Output goes in tempKey
		 */ 
		randBytes(tempKey,KLEN_2X);
	}

	// Copy values into the associated Keys in the object K
	memcpy(K->hmacKey, tempKey, KLEN_SKE); // lower tempKey
       	memcpy(K->aesKey, tempKey+KLEN_SKE, KLEN_SKE);	// upper tempKey
	return 0;
}

size_t ske_getOutputLen(size_t inputLen)
{
	return AES_BLOCK_SIZE + inputLen + HM_LEN;
}

size_t ske_encrypt(unsigned char* outBuf, unsigned char* inBuf, size_t len,
		SKE_KEY* K, unsigned char* IV)
{
	/* TODO: finish writing this.  Look at ctr_example() in aes-example.c
	 * for a hint.  Also, be sure to setup a random IV if none was given.
	 * You can assume outBuf has enough space for the result. */
	//outBuf is the CT, inBuf is the message,
	//const unsigned char *Aes = K->aesKey;
	if(!IV)//non IV was given
	randBytes(IV,16);//we generate random IV of size 16

	// Encrypt	
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();//sets up context for CT
	EVP_EncryptInit_ex(ctx,EVP_aes_256_ctr(),0,K->aesKey,IV);//sets up for encryption
	int num;
	unsigned char ctBuf[len]; // to hold CT
	EVP_EncryptUpdate(ctx,ctBuf,&num,inBuf,len);//does the encryption, now outBuf holds the aesCT
	//now we use hmac on the ct
	unsigned char temphmacKey[HM_LEN];//will hold the hmac of the CT
	//    hash func,     hmac K,   32, , CT   ,32 , holds hmac of CT
	HMAC(EVP_sha256(),K->hmacKey,HM_LEN,ctBuf,len,temphmacKey,NULL);
	printf("HASH1: %s\n",temphmacKey);
	// now we concat the IV+outBuf+temphmackey as our new outBuf which will be the CT
	memcpy(outBuf, IV, 16);//IV has size 16
	memcpy(outBuf+16, ctBuf, num);//size of len
	memcpy(outBuf+16+num,temphmacKey,HM_LEN);
	EVP_CIPHER_CTX_free(ctx);//free up space
	return num;//returns number of btyes written
		 /* TODO: should return number of bytes written, which
	             hopefully matches ske_getOutputLen(...). */
}

size_t ske_encrypt_file(const char* fnout, const char* fnin,
		SKE_KEY* K, unsigned char* IV, size_t offset_out)
{
	/* DONE: write this.  Hint: mmap. */

	// Variables
	int fdIn, fdOut;	// File Descriptor
	struct stat st;		// File Stats
	size_t fileSize, num;	// File Size & Bytes Written
	unsigned char* mappedFile;	// for mmap

	// Open Message File with Read Only Capability
	fdIn = open(fnin,O_RDONLY);
	// Error Check
	if (fdIn < 0){
		perror("Error:");
		return 1;
	}

	// Get File Size 
	stat(fnin, &st);
	fileSize = st.st_size; //-offset_out;

	// Mmap - see ske_decrypt_file() for more info
	mappedFile = mmap(NULL, fileSize,
			PROT_READ,MMAP_SEQ, fdIn, offset_out);
	// Error Check
	if (mappedFile == MAP_FAILED){
		perror("Error:");
		return 1;
	}

	// Create a temporary buffer to hold encrypted text
	unsigned char tempBuf[fileSize];

	// Call ske_encrypt
	num = ske_encrypt(tempBuf,mappedFile,fileSize,K,NULL);
	//**SHOULD I CHANGE IN ENCRYPT FUNCTION IV TO NULL?
	
	// Create Output File with RWX Capability
	fdOut = open(fnout,O_RDWR|O_CREAT,S_IRWXU);
	// Error Check
	if (fdOut < 0){
		perror("Error:");
		return 1;
	}

	// Write tempBuf to file
	int wc = write(fdOut,tempBuf,num);
	// Error check
	if (wc < 0){
		perror("Error:");
		return 1;
	}

	// Close Files & Delete Mappings
	close(fdIn);
	close(fdOut);
	munmap(mappedFile, fileSize);

	// Return number of bytes written
	return num;
}

size_t ske_decrypt(unsigned char* outBuf, unsigned char* inBuf, size_t len,
		SKE_KEY* K)
{
	/* TODO: write this.  Make sure you check the mac before decypting!
	 * Oh, and also, return -1 if the ciphertext is found invalid.
	 * Otherwise, return the number of bytes written.  See aes-example.c
	 * for how to do basic decryption. */

	/* Arguments
	 * outBuf = plaintext
	 * inBuf = cyphertext
	 * len = length of cyphertext
	 * K = key */
	// Split inBuf to IV, CT, HMAC buffers
	unsigned char 	ivBuf[AES_BLOCK_SIZE],
			ctBuf[len-AES_BLOCK_SIZE-HM_LEN],
			hmacBuf[HM_LEN];
	
	size_t ctSize = len-AES_BLOCK_SIZE-HM_LEN;

	memcpy(ivBuf,inBuf,AES_BLOCK_SIZE);
	memcpy(ctBuf,inBuf+AES_BLOCK_SIZE,ctSize);
	memcpy(hmacBuf,inBuf+len-HM_LEN,HM_LEN);

	// generate hash using cyphertext to ensure integrity of CT
	unsigned char tempHash[HM_LEN]; // to hold return of HMAC
	HMAC(EVP_sha256(),K->hmacKey,HM_LEN,ctBuf,ctSize,tempHash,NULL);
	printf("HASH2: %s\n",tempHash);
	// check hash
	size_t i;
	for (i=0;i<32;i++) {
		if(tempHash[i] != K->hmacKey[i]) return -1;
	}
	if(memcmp(hmacBuf,tempHash,HM_LEN)) return -1;

	// Decryption
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new(); // cyphertext context
	EVP_DecryptInit_ex(ctx,EVP_aes_256_ctr(),0,K->aesKey,ivBuf); // Initialize decryption
	int num; 
	EVP_DecryptUpdate(ctx,outBuf,&num,ctBuf,ctSize); // Decryption. outBuf holds plaintext
	EVP_CIPHER_CTX_free(ctx);
	return num; // number of bytes written
}

/* For decrypting contents of a file
 *
 * precondition:
 * fnout = file name of the file with the decrypted ct
 * fnin = file name of the file to be decrypted
 * K = the object containg the HMAC and AES key
 * offset_in = offset of file in 
 *
 * postcondition:
 * creates a new file with the decryption
 * returns number of bytes written
 */
size_t ske_decrypt_file(const char* fnout, const char* fnin,
		SKE_KEY* K, size_t offset_in)
{
	/* TODO: write this. */
	
	// Variables
	int fdIn, fdOut;	// File Descriptor 
	struct stat st; 	// File Stats
	size_t fileSize, num;	// File Size & Bytes written
	unsigned char* mappedFile;	// for memory map (mmap)


	// Open Encrypted File with Read Only Capability
	fdIn = open(fnin,O_RDONLY);
	// Error Check
	if (fdIn < 0){
		perror("Error:");
		return 1;
	}

	// Get File Size
	stat(fnin, &st);
	fileSize = st.st_size-offset_in;

	// Memory map the file with mmap
	/* Description of pa=mmap(addr, len, prot, flags, fildes, off);
	 *
	 * establishes a mapping b/w the address space of the process
	 * at the addres 'pa' for 'len' bytes to the memory obj
	 * represented by the file descriptor 'fildes' at the
	 * offset 'off' for 'len' bytes. 
	 *
	 * returns the address at which the mapping was placed
	 *
	 * addr == NULL,  kernel decides which address to mmap at
	 * len == fileSize of the file
	 * prot == R page protection
	 * flags == determined by professor
	 * fildes = fdIn, the fd to map
	 * off = offset from beginning of file
	 */
	 mappedFile = mmap(NULL, fileSize, 
	 		PROT_READ,MMAP_SEQ, fdIn, offset_in);
	// Error Check
	 if (mappedFile == MAP_FAILED){
	 	perror("Error:");
	 	return 1;
	 }

 	// Create a temporary buffer to hold decrypted text
	unsigned char tempBuf[fileSize]; 	

	// Call ske_decrypt
	num = ske_decrypt(tempBuf,mappedFile,fileSize,K);
	
	// Create Output File with R,W,& Execute Capability
	fdOut = open(fnout,O_RDWR|O_CREAT,S_IRWXU);
	// Error Check
	if (fdOut < 0){
		perror("Error:");
		return 1;
	}
	
	//**** DOUBLE CHECK THAT WRITE TO TEMPBUF IS OKAY
	//CHECK THE NUM = SIZE OF BYTES
	//OR SHOULD I MANULLY CHECK
	//DO I HAVE TO USE SKE_OUTPUTSIZE
	//WHAT ABOUT HMLEN OR AESBLOCK SIZE IN THAT FUNCTION?
	
	// Write tempBuf to file
	int wc = write(fdOut,tempBuf,num);
	// Error Check
	if ( wc < 0){
		perror("Error:");
		return 1;
	}

	// Close Files & Delete Mappings 
	close(fdIn);
	close(fdOut);
	munmap(mappedFile, fileSize);
	
	// Return number of bytes written
	return num;
}
