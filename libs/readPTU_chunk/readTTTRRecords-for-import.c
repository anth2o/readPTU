//
//  main.c
//  ptu-read-tests
//
//  Created by Raphaël Proux on 03/10/2017.
//  Copyright © 2017 Raphaël Proux. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>

// RecordTypes
#define rtPicoHarpT3     0x00010303    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $03 (PicoHarp)
#define rtPicoHarpT2     0x00010203    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $03 (PicoHarp)
#define rtHydraHarpT3    0x00010304    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $04 (HydraHarp)
#define rtHydraHarpT2    0x00010204    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $04 (HydraHarp)
#define rtHydraHarp2T3   0x01010304    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $04 (HydraHarp)
#define rtHydraHarp2T2   0x01010204    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $04 (HydraHarp)
#define rtTimeHarp260NT3 0x00010305    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $05 (TimeHarp260N)
#define rtTimeHarp260NT2 0x00010205    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $05 (TimeHarp260N)
#define rtTimeHarp260PT3 0x00010306    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T3), HW: $06 (TimeHarp260P)
#define rtTimeHarp260PT2 0x00010206    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $06 (TimeHarp260P)

// How big the file chunking will be
#define RECORD_CHUNK 512

// ================================================
// Buffer for keeping track of records
// ================================================
typedef struct {
    uint64_t timetag;
    int channel;
} record;

typedef struct {
    record records[RECORD_CHUNK]; // using this will improve memory locality
    size_t head;  // to keep track of what was the last read record in buffer
    size_t count; // if don't have enough photons, for example due to many records being oflcorrection flags
} record_buf_t;

void record_buf_reset(record_buf_t *buffer) {
    buffer->head = 0;
    buffer->count = 0;
}

void record_buf_pop(record_buf_t *buffer, uint64_t *timetag, int *channel) {
    size_t head = buffer->head;
    *timetag = buffer->records[head].timetag;
    *channel = buffer->records[head].channel;
    buffer->head = head + 1;
}

void record_buf_push(record_buf_t *buffer, uint64_t timetag, int channel) {
    size_t count = buffer->count;
    buffer->records[count].timetag = timetag;
    buffer->records[count].channel = channel;
    buffer->count = count + 1;
}
// ================================================
// END Buffer for keeping track of records
// ================================================

// ====================================================================
// CIRCULAR BUFFER Structure for use with the g2 algorithm based on it
// ====================================================================
typedef struct {
    uint64_t *buffer;
    size_t head;
    size_t count;
    size_t size; //of the buffer
} circular_buf_t;


int circular_buf_reset(circular_buf_t * cbuf);
int circular_buf_put(circular_buf_t * cbuf, uint64_t data);
int circular_buf_oldest(circular_buf_t * cbuf, uint64_t * data);


int circular_buf_reset(circular_buf_t * cbuf)
{
    int r = -1;
    
    if(cbuf)
    {
        cbuf->head = 0;
        cbuf->count = 0;
        r = 0;
    }
    
    return r;
}

int circular_buf_put(circular_buf_t * cbuf, uint64_t data)
{
    int r = -1;
    
    if(cbuf)
    {
        cbuf->buffer[cbuf->head] = data;
        cbuf->head = (cbuf->head + 1) % cbuf->size;
        if(cbuf->count < cbuf->size) {
            cbuf->count = cbuf->count + 1;
        }
        
        r = 0;
    }
    
    return r;
}

int circular_buf_oldest(circular_buf_t * cbuf, uint64_t * data) {
    int r = -1;
    
    // CAUTION: Even if the buffer is empty oldest will return whatever
    // is in buffer[0]. We do so because it is conveninet for our specific
    // application but it can be catastrophic.
    // TAKE HOME MESSAGE: Don't use this implementation as is for anything
    // other than computing g2.
    if(cbuf && data) {
        if(cbuf->count < cbuf->size) {
            *data = cbuf->buffer[0];
        } else {
            *data = cbuf->buffer[cbuf->head];
        }
        
        r = 0;
    }
    
    return r;
}
// ============================
// END OF CIRCULAR BUFFER
// ============================

int c_fseek(FILE *filehandle, long int offset)
{
    return fseek(filehandle, offset, SEEK_SET);
}

void ProcessPHT2(FILE* filehandle, record_buf_t *buffer,  uint64_t *oflcorrection)
{
    /*
     ProcessPHT2() reads the next records of a file until it finds a photon, and then returns.
     Inputs:
     filehandle         FILE pointer with an open record file to read the photons
     oflcorrection      pointer to an unsigned integer 64 bits. Will record the time correction in the timetags due to overflow (see output for details).
     timetag            pointer to an unsigned integer 64 bits (see outputs for details).
     channel            pointer to an integer (see outputs for details).
     Outputs:
     filehandle         FILE pointer with reader at the position of last analysed record
     oflcorrection      offset time on the timetags read in the file, due to overflows.
     timetag            if a photon is read, timetag of this photon. Otherwise, timetag == 0. It already includes the overflow correction so the value can be used directly.
     channel            if a photon is read, channel of this photon. 0 will usually be sync and >= 1 other input channels. If the record is not a photon, channel == -1 for an overflow record, -2 for a marker record.
     */
    /* SHOULD WORK BUT FUNCTION NOT TESTED */
    
    const int T2WRAPAROUND = 210698240;
    union
    {
        unsigned int allbits;
        struct
        {
            unsigned time   :28;
            unsigned channel  :4;
        } bits;
        
    } Record;
    unsigned int markers;
    uint32_t TTTRRecord[RECORD_CHUNK];
    
    fread(&TTTRRecord, RECORD_CHUNK, sizeof(TTTRRecord) ,filehandle);
    for(size_t i = 0; i < RECORD_CHUNK; i++) {
        Record.allbits = TTTRRecord[i];
    
        if(Record.bits.channel == 0xF) //this means we have a special record
        {
            //in a special record the lower 4 bits of time are marker bits
            markers = Record.bits.time & 0xF;
            if(markers == 0) //this means we have an overflow record
            {
                record_buf_push(buffer, 0, -1);
                *oflcorrection += T2WRAPAROUND; // unwrap the time tag overflow
            }
            else //a marker
            {
            //Strictly, in case of a marker, the lower 4 bits of time are invalid
            //because they carry the marker bits. So one could zero them out.
            //However, the marker resolution is only a few tens of nanoseconds anyway,
            //so we can just ignore the few picoseconds of error.
            record_buf_push(buffer, *oflcorrection + Record.bits.time, -2);
            }
        }
        else
        {
            if((int)Record.bits.channel > 4) //Should not occur
            {
                record_buf_push(buffer, 0, -3);
            }
            else
            {
                record_buf_push(buffer,
                                *oflcorrection + Record.bits.time,
                                Record.bits.channel);
            }
        }
    }
}

void ProcessHHT2(FILE* filehandle, int HHVersion, record_buf_t *buffer,  uint64_t *oflcorrection)
{
    /*
     ProcessHHT2() reads the next records of a file until it finds a photon, and then returns.
     Inputs:
     filehandle         FILE pointer with an open record file to read the photons
     HHVersion          Hydrahard version 1 or 2. Depends on record type specification in the header.
     oflcorrection      pointer to an unsigned integer 64 bits. Will record the time correction in the timetags due to overflow (see output for details).
     timetag            pointer to an unsigned integer 64 bits (see outputs for details).
     channel            pointer to an integer (see outputs for details).
     Outputs:
     filehandle         FILE pointer with reader at the position of last analysed record
     oflcorrection      offset time on the timetags read in the file, due to overflows.
     timetag            if a photon is read, timetag of this photon. Otherwise, timetag == 0. It already includes the overflow correction so the value can be used directly.
     channel            if a photon is read, channel of this photon. 0 will usually be sync and >= 1 other input channels. If the record is not a photon, channel == -1 for an overflow record, -2 for a marker record.
     */
    /* FUNCTION TESTED */
    const uint64_t T2WRAPAROUND_V1 = 33552000;
    const uint64_t T2WRAPAROUND_V2 = 33554432;
    union{
        uint32_t   allbits;
        struct{ unsigned timetag  :25;
            unsigned channel  :6;
            unsigned special  :1; // or sync, if channel==0
        } bits;
    } T2Rec;
    uint32_t TTTRRecord[RECORD_CHUNK];

    fread(TTTRRecord, RECORD_CHUNK, sizeof(uint32_t) ,filehandle);
    for(size_t i = 0; i < RECORD_CHUNK; i++) {
        T2Rec.allbits = TTTRRecord[i];
    
        if(T2Rec.bits.special==1)
        {
            if(T2Rec.bits.channel==0x3F) //an overflow record
            {
                if(HHVersion == 1)
                {
                    record_buf_push(buffer, 0, -1);
                    *oflcorrection += T2WRAPAROUND_V1;
                }
                else
                {
                    //number of overflows is stored in timetag
                    if(T2Rec.bits.timetag==0) //if it is zero it is an old style single overflow
                    {
                        *oflcorrection += T2WRAPAROUND_V2;  //should never happen with new Firmware! ///
                    }
                    else
                    {
                        *oflcorrection += T2WRAPAROUND_V2 * T2Rec.bits.timetag; ///
                    }
                }

                record_buf_push(buffer, 0, -1);
            }
            
            if((T2Rec.bits.channel>=1)&&(T2Rec.bits.channel<=15)) //markers
            {
                //Note that actual marker tagging accuracy is only some ns.
                record_buf_push(buffer, *oflcorrection + T2Rec.bits.timetag, -2);
                
            }
            
            else if(T2Rec.bits.channel==0) //sync
            {
                 record_buf_push(buffer, *oflcorrection + T2Rec.bits.timetag, T2Rec.bits.channel);
            }
        }
        else //regular input channel
        {
            record_buf_push(buffer,
                            *oflcorrection + T2Rec.bits.timetag,
                            T2Rec.bits.channel + 1);
        }
    }
}


void RecordHHT2(FILE* filehandle)
{
    /*
     RecordHHT2() is a function written for test purposes only. It writes a dummy file containing prearranged photons to test the timetrace and g2 algorithms.
     */
    union {
        uint32_t   allbits;
        struct{ unsigned timetag  :25;
            unsigned channel  :6;
            unsigned special  :1; // or sync, if channel==0
        } bits;
    } T2Rec;
    uint32_t TTTRRecord = 0;
    int i = 0;
    for(i = 0; i < 100; i++){
        if (i % 2){
            T2Rec.bits.special = 0;  // regular input channel
            T2Rec.bits.channel = 0;  // channel number
            T2Rec.bits.timetag = i * 500 + 10;  // timetag in ps
        }
        else {
            T2Rec.bits.special = 1;  // sync channel
            T2Rec.bits.channel = 0;  // sync channel
            T2Rec.bits.timetag = i * 500 + 10;  // timetag in ps
        }
        TTTRRecord = T2Rec.allbits;
        fwrite(&TTTRRecord, 4, 1, filehandle);
    }
}


int next_photon(FILE* filehandle, long long record_type, uint64_t *RecNum, uint64_t NumRecords, record_buf_t *buffer, uint64_t *oflcorrection, uint64_t *timetag, int *channel)
{
    /*
     next_photon() reads the next records of a file until it finds a photon, and then returns.
     Inputs:
     filehandle         FILE pointer with an open record file to read the photons
     record_type        record type which depends on the device which recorded the file (see constants at the beginning of file)
     RecNum             pointer to the index of the record being read
     NumRecords         total number of records
     oflcorrection      pointer to an unsigned integer 64 bits. Will record the time correction in the timetags due to overflow. At the start of the file should be provided as 0 for initial value.
     timetag            pointer to an unsigned integer 64 bits. Timetag of the next photon (see outputs for details).
     channel            pointer to an integer. Channel of the next photon (see outputs for details).
     Outputs:
     filehandle         FILE pointer with reader at the position of last analysed record
     RecNum             index of last analysed record
     oflcorrection      offset time on the timetags read in the file, due to overflows. Should not be used.
     timetag            timetag of the last photon read. It already includes the overflow correction so the value can  be used directly.
     channel            channel of the last photon read. 0 will usually be sync and >= 1 other input channels.
     Returns:
     1 when found a photon,
     0 when reached end of file.
     */
    
    // We may sacrifice up to RECORD_CHUNK records at the end of the file in order to simplify the logic of the function.
    if (buffer->head < RECORD_CHUNK && buffer->count > 0) { // still have records on buffer
        pop_record:
        do {
            record_buf_pop(buffer, timetag, channel);
            *RecNum = *RecNum + 1;
        } while(*channel < 0 && buffer->head < RECORD_CHUNK);

        if (channel >= 0) {
            return 1;
        }
        else { // we run out of buffer before finding a photon
            goto replenish_buffer;
        }
    } else {
        replenish_buffer:
        // we need to replenish the photon pool
        record_buf_reset(buffer);
        if ((*RecNum+RECORD_CHUNK) < NumRecords) {
            switch (record_type) {
                case rtPicoHarpT2:
                    ProcessPHT2(filehandle, buffer, oflcorrection);
                    break;
                case rtPicoHarpT3:
                    //ProcessPHT3(TTTRRecord);
                    break;
                case rtHydraHarpT2:
                    ProcessHHT2(filehandle, 1, buffer, oflcorrection);
                    break;
                case rtHydraHarpT3:
                    //ProcessHHT3(TTTRRecord, 1);
                    break;
                case rtHydraHarp2T2:
                case rtTimeHarp260NT2:
                case rtTimeHarp260PT2:
                    ProcessHHT2(filehandle, 2, buffer, oflcorrection);
                    break;
                case rtHydraHarp2T3:
                case rtTimeHarp260NT3:
                case rtTimeHarp260PT3:
                    //ProcessHHT3(TTTRRecord, 2);
                    break;
                default:
                    return 0;
            }
            goto pop_record;
        }
        return 0; // if we didn't had enough records to replenish
                  // the buffer we are done.
    }
    
}

void timetrace(FILE* filehandle, long long record_type, int end_of_header, uint64_t *RecNum, uint64_t NumRecords, uint64_t time_bin_length, uint64_t *time_vector, int *time_trace, uint64_t *RecNum_trace, int nb_of_bins)
{
    /*
     timetrace() computes the timetrace of a given measurement file. It does not differentiate the channel detecting the photons, which are all added together.
     Inputs:
     filehandle         FILE pointer with an open record file to read the photons
     record_type        record type which depends on the device which recorded the file (see constants at the beginning of file)
     end_of_header      offset in bytes to the beginning of the record section in the file
     RecNum             pointer to the index of the record being read
     NumRecords         total number of records
     time_bin_length    length of a time bin of the timetrace in picoseconds
     time_vector        preallocated array used for the x-axis of the timetrace. Should have nb_of_bins elements.
     time_trace         preallocated array used for the timetrace value. Should have nb_of_bins elements.
     RecNum_trace       preallocated array used for the values of RecNum corresponding to the last photon of each time bin. Should have nb_of_bins elements.
                        We lose resolution due to the fact that we are reading chunks now.
     nb_of_bins         number of bins for the timetrace (should correspond to the length of the time_trace array)
     Outputs:
     filehandle         FILE pointer with reader at the position of last analysed record
     RecNum             index of last analysed record
     time_trace         calculated timetrace
     */
    // IMPORTANT NOTE: every time in picoseconds
    record_buf_t record_buffer;
    record_buf_reset(&record_buffer);
    
    uint64_t oflcorrection = 0;
    uint64_t timetag = 0;
    int channel = -1;
    uint64_t end_of_bin = 0;
    int add_photon_to_next_bin = 0;
    size_t i = 0;
    int photon_bool = 1;
    
    // reset file reader
    c_fseek(filehandle, end_of_header);
    
    for (i = 0; i < nb_of_bins; i++)
    {
        time_vector[i] = i * time_bin_length;
        end_of_bin = time_vector[i] + time_bin_length;
        // the last timetag read is in the bin
        if (timetag < end_of_bin) {
            // if we are starting (still didn't read a photon), add_photon_to_next_bin == 0, otherwise, == 1
            time_trace[i] = add_photon_to_next_bin;
            add_photon_to_next_bin = 0; 

            photon_bool = next_photon(filehandle, record_type, RecNum, NumRecords,
                                          &record_buffer, &oflcorrection, &timetag, &channel);
            while(photon_bool == 1) {
                if(timetag < end_of_bin) { // photon is in the current bin
                    time_trace[i] = time_trace[i] + 1;
                }
                else { // belongs to some further bin (CAUTION: may not be the one immediately after)
                    RecNum_trace[i] = *RecNum;
                    add_photon_to_next_bin = 1;
                    break;
                }
                photon_bool = next_photon(filehandle, record_type, RecNum, NumRecords,
                                          &record_buffer, &oflcorrection, &timetag, &channel);
            }
            if (photon_bool == 0) {  // for the last time bin
                RecNum_trace[i] = *RecNum;
            }
        }
        // not photon found in this time bin.
        else {
            time_trace[i] = 0;
        }
    }
}

// void calculate_g2_fast(FILE* filehandle, long long record_type, int end_of_header, uint64_t *RecNum, uint64_t NumRecords, uint64_t RecNum_start, uint64_t RecNum_stop, uint64_t *time_vector, int *histogram, int nb_of_bins, int channel_start, int channel_stop)
// {
//     /*
//      calculate_g2_fast() computes the g2 directly reading the measurement file. It uses a simple algorithm which stops at the first stop photon (histogram mode style). This algorithm is fast but loses some information and can exhibit an exponential decay artefact linked to the photon rates.
//      Inputs:
//      filehandle         FILE pointer with an open record file to read the photons
//      record_type        record type which depends on the device which recorded the file (see constants at the beginning of file)
//      end_of_header      offset in bytes to the beginning of the record section in the file
//      RecNum             pointer to the index of the record being read
//      NumRecords         total number of records
//      RecNum_start       start of the section of records to analyse for the g2 (in terms of record index)
//      RecNum_stop        stop of the section of records to analyse for the g2
//      time_vector        precalculated array of times used for the x-axis of the g2 histogram. Should have nb_of_bins + 1 elements.
//      histogram          preallocated array of zeros used for the g2 histogram. Should have nb_of_bins elements.
//      nb_of_bins         number of bins for the histogram (should correspond to the length of the histogram array)
//      channel_start      channel number used for start photons (sync will generally be 0)
//      channel_stop       channel number used for stop photons (> 0, often 1)
//      Outputs:
//      filehandle         FILE pointer with reader at the position of last analysed record
//      RecNum             index of last analysed record
//      histogram          calculated g2 histogram
//      */

//     photon_buf_t photon_buffer;
//     photon_buf_reset(&photon_buffer);
    
//     uint64_t start_time = 0;
//     uint64_t stop_time = 0;
//     uint64_t oflcorrection = 0;
//     uint64_t timetag = 0;
//     int channel = -1;
//     size_t i = 0;
//     uint64_t correlation_window = 0;
//     int photon_bool = 1;
//     //    long next_print = 0;
//     correlation_window = time_vector[nb_of_bins];
    
//     // reset file reader and go to the start position RecNum_start
//     c_fseek(filehandle, end_of_header + 4 * RecNum_start);
//     *RecNum = RecNum_start;
    
//     // go to the start position RecNum_start
//     while(*RecNum < RecNum_stop && photon_bool){
//         //        if (*RecNum > next_print){
//         //            printf("%ld/%ld\n", *RecNum, RecNum_stop);
//         //            next_print = next_print + 1000000;
//         //        }
        
//         // FIND NEXT START PHOTON
//         channel = -1;
//         while(*RecNum < RecNum_stop && photon_bool==1 && channel != channel_start){
//             photon_bool = next_photon(filehandle, record_type, RecNum, NumRecords, 
//                                       &photon_buffer, &oflcorrection, &timetag, &channel);
//         }
//         if (*RecNum >= RecNum_stop || *RecNum >= NumRecords){
//             break;
//         }
//         // found a start photon
//         else {
//             start_time = timetag;
//         }
        
//         // FIND NEXT STOP PHOTON
//         while (*RecNum < RecNum_stop && photon_bool==1 && channel != channel_stop) {
//             photon_bool = next_photon(filehandle, record_type, RecNum, NumRecords, 
//                                       &photon_buffer, &oflcorrection, &timetag, &channel);
//         }
//         // found a stop photon
//         if (channel == channel_stop) {
//             stop_time = timetag;
//         }
        
//         // ADD DELAY TO HISTOGRAM
//         // add occurence to result histogram if the delay is in the correlation window
//         if (stop_time - start_time < correlation_window) {
//             i = (size_t) (stop_time - start_time) * nb_of_bins / correlation_window;
//             histogram[i] = histogram[i] + 1;
//         }
//     }
// }

void calculate_g2_ring(FILE* filehandle, long long record_type, int end_of_header,
                       uint64_t *RecNum, uint64_t NumRecords, uint64_t RecNum_start,
                       uint64_t RecNum_stop, uint64_t *time_vector, int *histogram,
                       int nb_of_bins, int channel_start, int channel_stop,
                       int buffer_size)
{
    /*
     calculate_g2() computes the g2 directly reading the measurement file. It uses a more complex algorithm than calculate_g2_fast(). This function will keep all photons in memory buffers, such that each start photon will be measured in regard of all the stop photons detected in a correlation window around it. This way, the measurement does not stop at the first stop photon but will take into account longer time scales. It is therefore safer to use with high photon count rates.
     Inputs:
     filehandle         FILE pointer with an open record file to read the photons
     record_type        record type which depends on the device which recorded the file (see constants at the beginning of file)
     end_of_header      offset in bytes to the beginning of the record section in the file
     RecNum             pointer to the index of the record being read
     NumRecords         total number of records
     RecNum_start       start of the section of records to analyse for the g2 (in terms of record index)
     RecNum_stop        stop of the section of records to analyse for the g2
     time_vector        precalculated array of times used for the x-axis of the g2 histogram. Should have nb_of_bins + 1 elements.
     histogram          preallocated array of zeros used for the g2 histogram. Should have nb_of_bins elements.
     nb_of_bins         number of bins for the histogram (should correspond to the length of the histogram array)
     channel_start      channel number used for start photons (sync will generally be 0)
     channel_stop       channel number used for stop photons (> 0, often 1)
     buffer_size        Size of the ring buffer
     Outputs:
     filehandle         FILE pointer with reader at the position of last analysed record
     RecNum             index of last analysed record
     histogram          calculated g2 histogram
     */

    record_buf_t record_buffer;
    record_buf_reset(&record_buffer);
    
    uint64_t oflcorrection = 0;
    uint64_t timetag = 0;
    uint64_t oldest_timetag;
    int channel = -1;
    
    uint64_t i;  // loop indexing
    uint64_t idx;  // index for histogram
    
    uint64_t delta;
    uint64_t new_correlation_window;
    uint64_t min_correlation_window = 18e18;  // almost 2^64
    uint64_t max_correlation_window = time_vector[nb_of_bins];

    int photon_bool = 1;
    
    // reset file reader and go to the start position RecNum_start
    c_fseek(filehandle, end_of_header + 4 * RecNum_start);
    *RecNum = RecNum_start;
    
    // Prepare the circular buffer for the start photons
    circular_buf_t cbuf;
    cbuf.size = buffer_size;
    circular_buf_reset(&cbuf);
    cbuf.buffer = calloc(cbuf.size, sizeof(uint64_t)); // set memory to zero so we have a proper
    // starting time.
    
    // Read all the photons
    photon_bool = next_photon(filehandle, record_type, RecNum, NumRecords,
                              &record_buffer, &oflcorrection, &timetag, &channel);

    while(photon_bool==1){
        photon_bool = next_photon(filehandle, record_type, RecNum, NumRecords,
                                  &record_buffer, &oflcorrection, &timetag, &channel);
        
        if (channel == channel_start) {
            circular_buf_put(&cbuf, timetag);
            circular_buf_oldest(&cbuf, &oldest_timetag);
            new_correlation_window = timetag - oldest_timetag;
            
            if (new_correlation_window < min_correlation_window) {
                min_correlation_window = new_correlation_window;
            }
        }
        
        if (channel == channel_stop) {
            for(i = 0; i < cbuf.count; i++) {
                delta = timetag - cbuf.buffer[i];
                if (delta < max_correlation_window) {
                    idx = (uint64_t)(delta * nb_of_bins / max_correlation_window);
                    histogram[idx] = histogram[idx] + 1;
                }
            }
        }
    }
}
