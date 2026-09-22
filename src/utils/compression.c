/*
--- POCKET+ compression ---
Author: FISCHER BENJAMIN
Contact: benjamin.fischer@esa.int, ben.fischer@ymail.com
*/

#include "compression.h"

/* Configuration parameters for Pocket */
static uint_fast8_t anchorFrequency = 0; // 0... no anchor deltas, 1... also anchor deltas are included
static uint_fast16_t positiveUpdateFrequency = 20; //every 20 packets: Do a positive mask history update.
static uint_fast8_t maxProtectionLevel = 5; //the maximum protection level which could be reached
static int_fast16_t npBitsOffset = 0; //offset (number of words) for the non-predictable bits extraction. From left to right.

/* Debug variables */
static uint_fast16_t numberOfNpBits = 0; // number of non-predictable bits in the current compressed packet
static uint_fast8_t protectionLevel = 0; //the current protection level in the comressed packet

/* Initialize the main pointers for pocket */
static uint32_t positive[P_P_WORDS_MAX_PACKET_SIZE]; //pointer for the positive buffer
static uint32_t mask[P_P_WORDS_MAX_PACKET_SIZE]; //pointer for the mask buffer
static uint32_t oldPacket[P_P_WORDS_MAX_PACKET_SIZE]; //pointer for the old packet buffer
static uint32_t newPacket[P_P_WORDS_MAX_PACKET_SIZE]; //pointer for the new packet buffer (this one will be reused in the logic block)

/* Information variables about the 32 bit input packet */
static int_fast16_t numberOfWords = 0; //array lenght of the four main buffers (positive, mask, oldPacket, newPacket)
static uint_fast16_t maxDeltaBits = 0; //max number of bits which are used for one delta
static uint_fast16_t numberOfBitsInPacket = 0; //how many bits are in one packet (32 bit - word level)
static uint_fast16_t packetSize = 0; //size of the reference packet

/* These two variables point to the current position of the compressed packet. New data will be appended here. */
static uint_fast16_t currentRegisterCompressedPacket = 0; // current register in the compressed packet.
static uint_fast8_t currentBitCompressedPacket = 3; //current bit position (kind of a pointer) of the compressed packet

/* Variables used for the compressed packet (output) */
static uint_fast16_t sizeCompressedPacket = 0; // size of the current compressed packet
static uint_fast16_t sizeOldCompressedPacket = 0; //size of the last compressed packet

/* Main pointers (buffers) for the protection level */
static uint16_t protectionLevelBufferNegative[P_P_MAX_PACKET_SIZE + P_P_PROTECTION_LEVEL];
//protection level buffer for the negative mask history (all negatives are stored here)
static uint16_t protectionLevelBufferPositive[P_P_MAX_PACKET_SIZE + P_P_PROTECTION_LEVEL];
//protection level buffer for the positive mask history (all positives are stored here)
static uint16_t protectionLevelNumberOfDeltasNegative[P_P_PROTECTION_LEVEL];
//buffer - how many negative deltas are stored (how many negative deltas do we have)
static uint16_t protectionLevelNumberOfDeltasPositive[P_P_PROTECTION_LEVEL];
//buffer - how many positive deltas are stored (how many positive deltas do we have)
static uint16_t newDeltaBuffer[P_P_MAX_PACKET_SIZE + P_P_PROTECTION_LEVEL];
//all new deltas are going into this buffer.

/* Counters for the positive and anchor update cycles */
static uint_fast16_t positiveMaskUpdateCounter = 20; //counter for the positive mask history update
static uint_fast16_t anchorPacketCounter = 100; //couner for the anchor update

/* Logic for the negative mask update. Updates the buffers newPacket, oldPacket, positive and mask */
static void negativeLogic(void){
	for(int_fast16_t word = 0; word < numberOfWords; word ++){
		/* Create the intermediate packet */
		newPacket[word] = newPacket[word] ^ oldPacket[word];

		/* Make the old packet the new packet using XOR trick */
		oldPacket[word] = oldPacket[word] ^ newPacket[word];

		/* Update the positive buffer */
		positive[word] = positive[word] | newPacket[word];

		/* New Mask = Intermediate Packet OR Mask */
		newPacket[word] = newPacket[word] | mask[word];

		/* Create the word array with the new negative deltas. */
		newPacket[word] = newPacket[word] ^ mask[word];

		/* Update the old mask with your current mask (negative update) */
		mask[word] = mask[word] ^ newPacket[word];
	}
}

/* Logic for the positive mask update. Updates the buffers newPacket, positive and mask */
static void positiveLogic(void) {
	for(int_fast16_t word = 0; word < numberOfWords; word ++){
		/* Generate a the buffer with all new predictable bit positions */
		newPacket[word] = positive[word] ^ mask[word];

		/* Set the mask to the positive  */
		mask[word] = positive[word];

		/* Reset positive to zero */
		positive[word] = 0;
	}
}

/* Extracts the deltas and returns the latest delta (32 bit). Uses the buffer newPacket and reads it from right to left. */
static uint32_t getDeltas( uint32_t word, uint_fast8_t * reset) {

	static const uint32_t MultiplyDeBruijnBitPosition[32] =
	{  1, 2, 29, 3, 30, 15, 25, 4, 31, 23, 21, 16, 26, 18, 5, 9,
	  32, 28, 14, 24, 22, 20, 17, 8, 27, 13, 19, 7, 12, 6, 11, 10
	};

	static uint32_t absBitPositionOld = 0;
	if(*reset){absBitPositionOld = 0;}
	*reset = 0;
	uint32_t absBitPositionNew = 0;
	uint32_t delta = 0;
	uint32_t LSB = 0;

	/* Determine the least significant bit and remove it from the buffer */
	LSB = (newPacket[word] & -newPacket[word]); //1010 -> 0010
	newPacket[word] = newPacket[word] - LSB; // remove the LSB. e.g. 1010 -> 1000

	/* Get the position of the LSB. e.g. 0010 -> =2 */
	absBitPositionNew = MultiplyDeBruijnBitPosition[((uint32_t)(LSB* 0x077CB531U)) >> 27];

	/* Determine the absolute bit position (right to left) and calculate and return the delta. */
	absBitPositionNew = absBitPositionNew + 32 * ( (numberOfWords-1) - word);
	delta = absBitPositionNew - absBitPositionOld;
	absBitPositionOld = absBitPositionNew;
	return delta;
}

/*
Writes a value and the corresponding number of bits (e.g. value = 23, bits = 8) to the end of the compressedPacket buffer.
Return values:
	Failure: -1
	Success: Size of the compressed packet;
 */
static int_fast16_t writeToCompressedPacket(uint16_t value, int_fast8_t numberOfBits, uint8_t * compressedPacket){
	/* Calculate the rest which does not fit into the current byte. A rest > 0 indicates that there is not enough space within the current register */
	int_fast8_t rest = numberOfBits - 8 + currentBitCompressedPacket;
	if (rest > 0){ // there is not enough space within the current register

		/* Check if we have enough memory */
		if( currentRegisterCompressedPacket >= (sizeCompressedPacket - 3) ){ //we are in the last register. there is not enough space anymore
			return -1;
		}

		/* Fill up the current register. */
		compressedPacket[currentRegisterCompressedPacket] = compressedPacket[currentRegisterCompressedPacket] | (uint8_t) (value >> rest);

		uint16_t mask = 1; // a mask for a bit-AND operation will be needed in the coming branches.

		/* Fill up the next register and increase the size of the compressed packet by 1 */
		if ( (rest < 9) ){
			for(uint_fast8_t i = 0; i<rest; i ++){mask *= 2;} //create the mask for the bitshift
			mask -= 1;
			compressedPacket[currentRegisterCompressedPacket + 1] = (uint8_t) ( (value & mask) << (8 - rest) );
			currentBitCompressedPacket = currentBitCompressedPacket+numberOfBits - 8;
			currentRegisterCompressedPacket += 1;
		}
		/* Fill up the next and afternext register and increase the size of the compressed packet by 2 */
		else{
			compressedPacket[currentRegisterCompressedPacket + 1] = (uint8_t) (value >> (rest - 8)) & 0xff;
			for(uint_fast8_t i = 0; i<(rest-8); i ++){mask *= 2;} //create the mask for the bitshift
			mask -= 1;
			compressedPacket[currentRegisterCompressedPacket + 2] = (uint8_t) (value & mask) << (16 - rest);
			currentBitCompressedPacket = currentBitCompressedPacket+numberOfBits - 16;
			currentRegisterCompressedPacket += 2;
		}
	}
	/* There is enough space in the current register */
	else{
		compressedPacket[currentRegisterCompressedPacket] = compressedPacket[currentRegisterCompressedPacket] | (uint8_t) (value << abs(rest));
		currentBitCompressedPacket += numberOfBits;
	}
	return currentRegisterCompressedPacket;
}

static void writeToCompressedHeader(uint_fast8_t value, uint8_t * compressedPacket){

	compressedPacket[0] |= (uint8_t) (value << 5);
}

/*
Apply the anchor logic to the last mask, extract the deltas and write them to the compressed packet.
Return values:
	Failure: -1
	Success:
		 0 ... no anchor deltas
		 4 ... anchor deltas were written
 */
static int_fast8_t writeAnchorDeltas(uint8_t * compressedPacket){
	uint_fast8_t type; // 2 bits. type of delta. 0...no delta, 1...delta = 1, 2 ... 5 bits for delta, 3 ... max bits for delta
	uint_fast8_t numberOfBits; // how many bits used for the delta
	uint32_t delta = 0; // the actual delta value (decimal)
	uint_fast8_t resetDeltas = 1; // reset value for the getDeltaFunction().

	for(int_fast16_t i = numberOfWords - 1 ; i >= 0; i--){ // Go through the mask buffer.
		newPacket[i] = mask[i] ^ (mask[i] >> 1); //do the HXOR

		/* Write the deltas to the compressed packet */
		while(newPacket[i]){
			delta = getDeltas( i, &resetDeltas);

			if(delta == 1){
				if( (writeToCompressedPacket(1, 2, compressedPacket)) == -1 ){return -1;} // write 01 to the compressed packet
			}
			else{
				if(delta < 34){
					type = 2;
					numberOfBits = 5;
				}
				else{
					type = 3;
					numberOfBits = maxDeltaBits;
				}
				if( (writeToCompressedPacket(type, 2, compressedPacket)) == -1 ){return -1;}
				if( (writeToCompressedPacket(delta - 2 , numberOfBits, compressedPacket)) == -1 ){return -1;}
			}
		}
	}
	if(delta){
		if( (writeToCompressedPacket(0, 2, compressedPacket)) == -1 ){return -1;}
		return 4;
	}
	return 0;
}

/*
Creates the negative and positive delta field (incl. protection level) in the compressed packet.
Return values:
	Failure: -1
	Success: Type/kind of delta fields
		0 ... no delta fields
		1 ... only negative deltas
		2 ... only positive deltas
		3 ... positive deltas followed by negative deltas
*/
static int_fast16_t getMaskChanges(uint_fast8_t typeId, uint8_t * compressedPacket, uint_fast8_t * newPacketReceived ){

	/* These two static variables provide information, which mask history updates have been done already */
	static uint8_t negativeSet = 0;
	static uint8_t positiveSet = 0;

	uint_fast8_t resetDeltas = 1; //Reset the getDeltas function, so that it starts from 0. (1 = Reset)
	uint32_t delta = 0; //the current delta

	uint_fast16_t numberOfNewDeltas; //number of new deltas we have in the new packet
	uint_fast16_t totalNumberOfDeltas = 0; //total number of deltas in our packet
	uint_fast16_t wrapAroundBitPosition = 0; //because of the protection level, there is a wrap around delta (see documentation)

	uint16_t * protectionLevelBufferPointer = NULL; //used to point to the required protection level buffer (i.e. protectionLevelBufferNegative or protectionLevelBufferPositive)
	uint16_t * protectionLevelNumberOfDeltasPointer = NULL; //used to point to the required "number of deltas protection level" buffer (i.e. protectionLevelNumberOfDeltasNegative or protectionLevelNumberOfDeltasPositive  )
	uint8_t * setPointer = NULL; //used to point to the required typeSet variable (i.e. negativeSet or positiveSet)

	/* Determine the type of mask update and assign the pointers to the appropriate buffers */
	switch (typeId){
		case 0:
			protectionLevelBufferPointer = protectionLevelBufferNegative;
			protectionLevelNumberOfDeltasPointer = protectionLevelNumberOfDeltasNegative;
			break;
		case 1:
			protectionLevelBufferPointer = protectionLevelBufferPositive;
			protectionLevelNumberOfDeltasPointer = protectionLevelNumberOfDeltasPositive;
			break;
		default:
			return -1;
	}

	/* When we receive a new packet, update the protection level buffers and reset some variables */
	if (*newPacketReceived){
		/*
		When the old packet is smaller then the reference packet, we increase the the protection level by 1 (up to the max. protection level)
		Otherwise (the old packet is greater then the reference packet), we decrease the protection level by 1 or reset it to 1 (old packet is 2 x reference packet)
		 */
		if(sizeOldCompressedPacket < packetSize){
			protectionLevel += (protectionLevel < maxProtectionLevel);
		}
		else{
			if ( (sizeOldCompressedPacket < (2 * packetSize) ) ){
				protectionLevel -= (protectionLevel > 1);
			}
			else{
				protectionLevel = 1;
			}
		}

		/* Reset some variables */
		negativeSet = 0; //no negatives set
		positiveSet = 0; //no positives set

		/* Shift the number-of-deltas buffers by 1 to the right.(5, 2, 8 -> 1, 5, 2) */
		memmove(protectionLevelNumberOfDeltasPositive + 1, protectionLevelNumberOfDeltasPositive, (protectionLevel - 1) * sizeof(uint16_t));
		protectionLevelNumberOfDeltasPositive[0] = 1;
		memmove(protectionLevelNumberOfDeltasNegative + 1, protectionLevelNumberOfDeltasNegative, (protectionLevel - 1) * sizeof(uint16_t));
		protectionLevelNumberOfDeltasNegative[0] = 1;

		/* Shift the protection level buffers by 1 to the right.(5, 2, 8 -> 0, 5, 2) */
	 	memmove(protectionLevelBufferPositive + 1, protectionLevelBufferPositive, sizeof(uint16_t) * (numberOfBitsInPacket - 1));
	 	memmove(protectionLevelBufferNegative + 1, protectionLevelBufferNegative, sizeof(uint16_t) * (numberOfBitsInPacket - 1));
	 	protectionLevelBufferPositive[0] = 0;
	 	protectionLevelBufferNegative[0] = 0;
	}

	/* Initialize the first entries */
	numberOfNewDeltas = 0;
	newDeltaBuffer[0] = 0;
	/* We go trough the buffer from right to left and store all new deltas in the newDeltaBuffer (16 bit) array */
	for(int_fast16_t i = numberOfWords - 1 ; i >= 0; i --){ // -1 because 0 is included
		while(newPacket[i]){
			delta = getDeltas( i, &resetDeltas);
			newDeltaBuffer[numberOfNewDeltas] = delta;
			numberOfNewDeltas += 1;
			wrapAroundBitPosition += delta;
		}
	}

	numberOfNewDeltas += (numberOfNewDeltas == 0); //if there was no delta, add 1 (this is equal to a delta of zero.)
 	protectionLevelNumberOfDeltasPointer[0] = numberOfNewDeltas; // assign the number of deltas of the new packet to the numberOfDeltas protection level buffer

 	/* If we have a new delta, update the protection level buffers and apply the new wrap around delta value. */
 	if(delta){
 		/* Check if we have enough memory space for the new deltas. If not, remove the oldest protection level by setting it to zero. */
		totalNumberOfDeltas = 0;
		for(uint_fast8_t i = 0; i < protectionLevel; i++){
			totalNumberOfDeltas += protectionLevelNumberOfDeltasPointer[i];
			if(totalNumberOfDeltas > numberOfBitsInPacket ){
				totalNumberOfDeltas -= protectionLevelNumberOfDeltasPointer[i];
				protectionLevelNumberOfDeltasPointer[i] = 1;
				protectionLevelBufferPointer[totalNumberOfDeltas] = 0;
				break;
			}
		}

		/* Move the protection level buffers to the right (by the number of new deltas) and copy the new deltas to the beginning.
		e.g.: new deltas: 24,13,2 ; old buffer: 32,1,5,2,9,1 ; new buffer: 24,13,2, 32,1,5,2
		 */
		memmove(protectionLevelBufferPointer + numberOfNewDeltas, protectionLevelBufferPointer + 1, sizeof(uint16_t) * (totalNumberOfDeltas - numberOfNewDeltas) );
		memcpy(protectionLevelBufferPointer, newDeltaBuffer, sizeof(uint16_t) * numberOfNewDeltas );

		/* Update the protection level with the wrap around delta. (see documentation) */
	 	for (uint_fast8_t i = 0; i < (protectionLevel - 1 ); i++){
			if(protectionLevelBufferPointer[numberOfNewDeltas + i]){
		 		if( protectionLevelBufferPointer[numberOfNewDeltas + i] < wrapAroundBitPosition ){
		 			protectionLevelBufferPointer[numberOfNewDeltas + i] += numberOfBitsInPacket - wrapAroundBitPosition;
		 		}
		 		else {
					protectionLevelBufferPointer[numberOfNewDeltas + i] -= wrapAroundBitPosition;
		 		}
		 		break;
			}
	 	}
 	}

	uint_fast8_t type = 0; //how many bits are used for the mask update (1 = 1 bit, 2 = 5 bit, 3 = max bits)
	uint_fast16_t numberOfBits = 0; //how many bits will be written to the compressed packet

	/* Write the mask history to the compressed packet */
	for(uint_fast8_t i2 = 0; i2 < 2; i2++){ // i2 < 2 because we can only have a negative and a positive mask update

		if( (positiveSet == 0) && (i2 == 0) ){ //do a positive mask update only if there has not been a positive update and do it in the first iteration. (positive updates are always first)
			//assign positive pointers
			setPointer = &positiveSet;
			protectionLevelBufferPointer = protectionLevelBufferPositive;
			protectionLevelNumberOfDeltasPointer = protectionLevelNumberOfDeltasPositive;
		}
		else if( typeId == 0 ) { // if the typeId is negative, write the negative mask update to the compressed packet.
			//assign negative pointers
			setPointer = &negativeSet;
			protectionLevelBufferPointer = protectionLevelBufferNegative;
			protectionLevelNumberOfDeltasPointer = protectionLevelNumberOfDeltasNegative;
		}

		/* ONLY when there has not been any mask update yet, write the mask history to the compressed packet. */
		if(*setPointer == 0){
			totalNumberOfDeltas = 0;
			for(uint_fast8_t i = 0; i < protectionLevel; i++){totalNumberOfDeltas += protectionLevelNumberOfDeltasPointer[i];} //count the deltas
			for(uint_fast16_t i = 0; i < totalNumberOfDeltas; i++){ //go trough the delta buffer and write it to the compressed packet
				if(protectionLevelBufferPointer[i]){
					if(protectionLevelBufferPointer[i] == 1){
						if( (writeToCompressedPacket(1, 2, compressedPacket)) == -1){return -1;} //write 01 to the compressed packet
					}
					else{
						if(protectionLevelBufferPointer[i] < 34){
							type = 2;
							numberOfBits = 5;
						}
						else{
							type = 3;
							numberOfBits = maxDeltaBits;
						}
						if( (writeToCompressedPacket(type, 2, compressedPacket)) == -1){return -1;} ///write delta type
						if( (writeToCompressedPacket(protectionLevelBufferPointer[i] - 2 , numberOfBits, compressedPacket)) == -1){return -1;} // delta value
					}
					*setPointer = 1; //mask update done!
				}
			}
			if(*setPointer){ //
				if( (writeToCompressedPacket(0 , 2, compressedPacket)) == -1){return -1;} // write 00 to the end
			}
		}
	}

	*newPacketReceived = 0; // this is not a new packet anymore

	/* return the packet type of mask updates done yet */
	return (negativeSet + positiveSet * 2);
}

/*
Extracts the non-predictable bits and writes them to the compressed packet.
Return values:
	Failure: -1
	Success: 0
*/
static int_fast8_t extractNpBits(uint8_t * compressedPacket) {

	uint16_t npBitsBuffer = 0; // Buffer for up to 16 non-predictable bits. (16 bit because writeToCompressedPacket can only handle up to 16 bits)
	uint_fast8_t counter = 16; // counter for filling up all 16 bits
	uint32_t x; // first temporary variable for the extraction process
	uint32_t copy; // second temporary variable for the extraction process
	numberOfNpBits = 0;

	for(int_fast16_t word = numberOfWords-1; word>=npBitsOffset; word--) { // go from the right to the left
		copy = mask[word]; //store the mask in the temporary variable copy
		while (copy){ //as long there is a value in copy -> continue
			/* Found a non-predictable bit position */
			numberOfNpBits += 1;
			counter--; // one space less in the npBitsBuffer
			x=copy & -copy; // gets the least significant standing bit
			if (x & oldPacket[word]){ // write only ones to the npBitsBuffer
				npBitsBuffer |= 1<<counter; // write the non-predictable with value 1 to the npBits buffer (left to right)
			}
			/*
			When the counter value is equal to zero, the npBitsBuffer is full.
			Write these non-predictable bits to the compressed packet, reset everything and continue.
			*/
			if (counter==0){
				if( (writeToCompressedPacket(npBitsBuffer, 16, compressedPacket) ) == -1 ){return -1;}
				npBitsBuffer=0;
				counter = 16;
			}
			copy = copy-x; //remove the LSB.
		}
	}

	/* When there is nothing more to read, the "rest" inside the NP-buffer must be written to the compressed packet as well */
	if (counter<16) {
		npBitsBuffer=npBitsBuffer>>counter;
		if( (writeToCompressedPacket(npBitsBuffer, (16 - counter), compressedPacket) ) == -1 ){return -1;}
	}
	return 0;
}

uint_fast16_t startPocket(uint32_t * referencePacket, uint_fast16_t referencePacketSize, uint_fast16_t positiveUpdateRate, uint_fast8_t maximumProtectionLevel, uint_fast8_t anchorUpdateFrequency, int_fast16_t npOffset){

	if(maximumProtectionLevel > positiveUpdateRate){return 0;}

	if(referencePacketSize > 8188){return 0;}

	if(maximumProtectionLevel == 0){return 0;}

	packetSize = referencePacketSize; //get the reference packet size
	anchorFrequency = anchorUpdateFrequency; // after how many packets should an anchor packet be added
	anchorPacketCounter = anchorFrequency;
	npBitsOffset = npOffset;
	maxProtectionLevel =  maximumProtectionLevel; //assign the max. protection level
	numberOfWords = packetSize / 4; //number of elements in the 32 bit word array
	numberOfBitsInPacket = numberOfWords * 32; //e.g.: 3 words ->  3*32 = 96 bits
	sizeCompressedPacket = referencePacketSize * 4;

	memset(protectionLevelBufferNegative, 0, (P_P_MAX_PACKET_SIZE + P_P_PROTECTION_LEVEL) * sizeof(uint16_t));
	memset(protectionLevelBufferPositive, 0, (P_P_MAX_PACKET_SIZE + P_P_PROTECTION_LEVEL) * sizeof(uint16_t));
	memset(protectionLevelNumberOfDeltasNegative, 0, P_P_PROTECTION_LEVEL * sizeof(uint16_t));
	memset(protectionLevelNumberOfDeltasPositive, 0, P_P_PROTECTION_LEVEL * sizeof(uint16_t));
	memset(newDeltaBuffer, 0, P_P_MAX_PACKET_SIZE + P_P_PROTECTION_LEVEL * sizeof(uint16_t));
	for(uint_fast8_t i = 0; i < maxProtectionLevel; i++) {
		protectionLevelNumberOfDeltasNegative[i] = 1;
		protectionLevelNumberOfDeltasPositive[i] = 1;
	} //initilize with ones

	/* Buffers for the deltas of the current and previous packets. */
	positiveUpdateFrequency = positiveUpdateRate;
	positiveMaskUpdateCounter = positiveUpdateFrequency;

	memset(oldPacket, 0, P_P_WORDS_MAX_PACKET_SIZE * sizeof(uint32_t));
	memcpy(oldPacket, referencePacket, sizeof(uint32_t)*numberOfWords); //copy the first packet into the old packet
	memset(positive, 0, P_P_WORDS_MAX_PACKET_SIZE * sizeof(uint32_t));
	memset(mask, 0, P_P_WORDS_MAX_PACKET_SIZE * sizeof(uint32_t));
	memset(newPacket, 0, P_P_WORDS_MAX_PACKET_SIZE * sizeof(uint32_t));


	/* Determine the max. number of bits for the deltaCounter field */
	if( (numberOfBitsInPacket > 31) && (numberOfBitsInPacket <= 63) ){maxDeltaBits = 6;}
	else if( (numberOfBitsInPacket > 63) && (numberOfBitsInPacket <= 127) ){maxDeltaBits = 7;}
	else if( (numberOfBitsInPacket > 127) && (numberOfBitsInPacket <= 255) ){maxDeltaBits = 8;}
	else if( (numberOfBitsInPacket > 255) && (numberOfBitsInPacket <= 511) ){maxDeltaBits = 9;}
	else if( (numberOfBitsInPacket > 511) && (numberOfBitsInPacket <= 1023) ){maxDeltaBits = 10;}
	else if( (numberOfBitsInPacket > 1023) && (numberOfBitsInPacket <= 2047) ){maxDeltaBits = 11;}
	else if( (numberOfBitsInPacket > 2047) && (numberOfBitsInPacket <= 4095) ){maxDeltaBits = 12;}
	else if( (numberOfBitsInPacket > 4095) && (numberOfBitsInPacket <= 8191) ){maxDeltaBits = 13;}
	else if( (numberOfBitsInPacket > 8191) && (numberOfBitsInPacket <= 16383) ) {maxDeltaBits = 14;}
	else if( (numberOfBitsInPacket > 16383) && (numberOfBitsInPacket <= 32767) ) {maxDeltaBits = 15;}
	else if( (numberOfBitsInPacket > 32767)){maxDeltaBits = 16;}

	return referencePacketSize;
}

void stopPocket(){
	return;
}

int_fast16_t compressPacket(uint8_t * compressedPacket, uint32_t * uncompressedPaket){

	/* +++ New Packet received +++ */
	uint_fast8_t newPacketReceived = 1; // variable used to reset the getMaskChanges() function.
	int_fast8_t packetHeader = 0; // stores the current packet header (3 bits)
	memset(compressedPacket, 0 , sizeCompressedPacket ); //reset the compressed packet memory.

	/* These to variables point to the current position in the compressed packet which will be written next */
	currentRegisterCompressedPacket = 0; //reset the current compressed packet register back to zero
	currentBitCompressedPacket = 3; //reset the bit position to 3 (because the first 3 bits are the header)

	int_fast8_t anchorFlag = 0; // return value for the anchor mode. Failure: -1, Success: 0 ... no deltas, 4 ... deltas were writen
	if( anchorFrequency && (anchorPacketCounter == 0) ){
		anchorFlag = writeAnchorDeltas(compressedPacket);
		anchorPacketCounter = anchorFrequency;
		if(anchorFlag == -1){return -1;}
	}

	/* Write the new positive mask updates to the compressed packet (periodicly) */
	if(positiveMaskUpdateCounter == 0){
		positiveLogic();
		positiveMaskUpdateCounter = positiveUpdateFrequency;
		packetHeader = getMaskChanges(1 , compressedPacket, &newPacketReceived);
		if(packetHeader == -1){return -1;}
	}

	/* Copy the new packet into memory */
	memcpy(newPacket, uncompressedPaket, packetSize );

	/* Create the negative mask updates */
	negativeLogic(); // call the logic for the negatives

	/* Write new negative mask updates to the the compressed packet */
	packetHeader = getMaskChanges(0, compressedPacket, &newPacketReceived);
	if(packetHeader == -1){return -1;}

	/* Add the non-predictable bits to the compressed packet */
	if((extractNpBits(compressedPacket))==-1){return -1;}

	/* Decrement the positive and anchor counters */
	positiveMaskUpdateCounter -= 1;
	anchorPacketCounter -= (anchorFrequency > 0);

	/* Store the size of the compressed packet.
	When this size becomes greater then the reference packet size, the protection level is decremented (done in getMaskChanges). */
	sizeOldCompressedPacket = currentRegisterCompressedPacket;

	writeToCompressedHeader( packetHeader + anchorFlag , compressedPacket); // write the packet header (3bits) to the compressed packet

	return currentRegisterCompressedPacket + 1 ; //return the total size (bytes) of the compressed packet.
}


uint_fast8_t getProtectionLevel(void){

	return protectionLevel;

}

void getMask(uint32_t * output_Mask){

	memcpy(output_Mask, mask, sizeof(*mask) * numberOfWords );

}

uint_fast16_t getNumberOfNpBits(void){

	return numberOfNpBits;

}
