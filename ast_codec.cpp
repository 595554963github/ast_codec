#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <climits>
#include <cstdint>
#include <windows.h>
#include <direct.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef int32_t s32;
typedef uint32_t u32;

static const short DSPADPCM_FILTER[16][2] = {
    {0, 0},
    {2048, 0},
    {0, 2048},
    {1024, 1024},
    {4096, -2048},
    {3584, -1536},
    {3072, -1024},
    {4608, -2560},
    {4200, -2248},
    {4800, -2300},
    {5120, -3072},
    {2048, -2048},
    {1024, -1024},
    {-1024, 1024},
    {-1024, 0},
    {-2048, 0}
};

template <typename T>
T swap_endian(T u) {
    static_assert(CHAR_BIT == 8, "CHAR_BIT != 8");
    union {
        T u;
        unsigned char u8[sizeof(T)];
    } source, dest;
    source.u = u;
    for (size_t k = 0; k < sizeof(T); k++)
        dest.u8[k] = source.u8[sizeof(T) - k - 1];
    return dest.u;
}

struct WAVHeader {
    char ChunkID[4] = { 'R','I','F','F' };
    u32 ChunkSize = 0;
    char Format[4] = { 'W','A','V','E' };
    char subChunk1ID[4] = { 'f','m','t',' ' };
    u32 Subchunk1Size = 16;
    u16 AudioFormat = 1;
    u16 NumChannels = 2;
    u32 SampleRate = 0;
    u32 ByteRate = 0;
    u16 BlockAlign = 0;
    u16 BitsPerSample = 16;
    char SubChunk2ID[4] = { 'd','a','t','a' };
    u32 Subchunk2Size = 0;
};

struct AST_Heading {
    char Name[4];
    u32 Size;
    u16 Format;
    u16 BitDepth;
    u16 NumChannels;
    u16 unk_1;
    u32 SampleRate;
    u32 TotalSamples;
    u32 LoopStart;
    u32 LoopEnd;
    u32 FirstBlockSize;
    u32 unk_2;
    u32 unk_3;
    u8 padding[20];
    void convertToLittleEndian() {
        Size = swap_endian(Size);
        Format = swap_endian(Format);
        BitDepth = swap_endian(BitDepth);
        NumChannels = swap_endian(NumChannels);
        unk_1 = swap_endian(unk_1);
        SampleRate = swap_endian(SampleRate);
        TotalSamples = swap_endian(TotalSamples);
        LoopStart = swap_endian(LoopStart);
        LoopEnd = swap_endian(LoopEnd);
        FirstBlockSize = swap_endian(FirstBlockSize);
        unk_2 = swap_endian(unk_2);
        unk_3 = swap_endian(unk_3);
    }
};

struct BLCK_Header {
    char Blck_Name[4];
    u32 Blck_Size;
    u8 padding[24];
};

struct Block_Data {
    std::vector<s16> data;
};

enum AST_FORMAT {
    AST_FORMAT_ADPCM = 0,
    AST_FORMAT_PCM16 = 1
};

int convertASTtoWAV(const char* filename, const char* outputname, u32 loopcount) {
    std::cout << "Converting " << filename << " to " << outputname << '\n';
    std::ifstream file(filename, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
        std::cout << "Error: Couldn't open file " << filename << "!\n";
        return -1;
    }

    char headerBuffer[sizeof(AST_Heading)];
    file.read(headerBuffer, sizeof(AST_Heading));
    AST_Heading* heading = reinterpret_cast<AST_Heading*>(headerBuffer);
    heading->convertToLittleEndian();

    std::vector<Block_Data> data(heading->NumChannels);
    size_t BytesRead = sizeof(AST_Heading);
    bool isReadingFile = true;

    while (isReadingFile) {
        char BLCK_Header_Buffer[sizeof(BLCK_Header)];
        file.read(BLCK_Header_Buffer, sizeof(BLCK_Header));
        BLCK_Header* block_header = reinterpret_cast<BLCK_Header*>(BLCK_Header_Buffer);

        if (strncmp(block_header->Blck_Name, "BLCK", 4) != 0) {
            std::cout << "ERROR: BLCK NOT VALID! Read Pos is 0x" << std::hex << file.tellg() << '\n';
            return -1;
        }

        u32 Blck_Size = swap_endian(block_header->Blck_Size);
        BytesRead += sizeof(BLCK_Header);
        file.seekg(BytesRead);

        for (int i = 0; i < heading->NumChannels; i++) {
            std::vector<char> Buffer(Blck_Size);
            file.read(Buffer.data(), Blck_Size);

            if (heading->Format == AST_FORMAT_ADPCM) {
                for (u32 j = 0; j < Blck_Size; j += 9) {
                    char* input = Buffer.data() + j;
                    s16 delta = 1 << (((*input) >> 4) & 0b1111);
                    s16 adpcm_id = (*input) & 0b1111;
                    s16 nibbles[16];
                    input++;

                    for (int k = 0; k < 16; k += 2) {
                        nibbles[k] = ((*input & 0xFF) >> 4);
                        nibbles[k + 1] = (*input & 0x0F);
                        input++;
                    }

                    for (int k = 0; k < 16; k++) {
                        if (nibbles[k] >= 8) nibbles[k] -= 16;

                        s16 hist = 0, hist2 = 0;
                        size_t data_size = data[i].data.size();
                        if (data_size > 0) hist = data[i].data[data_size - 1];
                        if (data_size > 1) hist2 = data[i].data[data_size - 2];

                        int sample = (delta * nibbles[k]) << 11;
                        sample += (hist * DSPADPCM_FILTER[adpcm_id][0]) + (hist2 * DSPADPCM_FILTER[adpcm_id][1]);
                        sample >>= 11;

                        if (sample > 32767) sample = 32767;
                        else if (sample < -32767) sample = -32767;

                        data[i].data.push_back((s16)sample);
                    }
                }
            }
            else if (heading->Format == AST_FORMAT_PCM16) {
                s16* data_array = reinterpret_cast<s16*>(Buffer.data());
                for (u32 j = 0; j < Blck_Size / 2; j++) {
                    data[i].data.push_back(swap_endian(data_array[j]));
                }
            }

            BytesRead += Blck_Size;
            file.seekg(BytesRead);
        }

        if (BytesRead >= heading->Size + 0x40) isReadingFile = false;
    }
    file.close();

    u32 fadeout_in_samples = (heading->LoopStart > 0 && loopcount > 0) ? 15 * heading->SampleRate : 0;

    WAVHeader out_header = {};
    out_header.NumChannels = heading->NumChannels;
    out_header.SampleRate = heading->SampleRate;
    out_header.BitsPerSample = 16;
    out_header.ByteRate = out_header.SampleRate * out_header.NumChannels * 2;
    out_header.BlockAlign = out_header.NumChannels * 2;
    out_header.Subchunk2Size = (heading->LoopEnd * out_header.NumChannels * 2) +
        (loopcount * (heading->LoopEnd - heading->LoopStart) * out_header.NumChannels * 2) +
        (fadeout_in_samples * out_header.NumChannels * 2);
    out_header.ChunkSize = 36 + out_header.Subchunk2Size;

    std::ofstream out_file(outputname, std::ios::binary);
    size_t outbuffer_size = sizeof(WAVHeader) + (heading->LoopEnd * 2 * heading->NumChannels);
    std::vector<char> out_buffer(outbuffer_size);
    memcpy(out_buffer.data(), &out_header, sizeof(WAVHeader));

    size_t data_size = heading->LoopEnd;
    for (u32 track_index = 0; track_index < heading->NumChannels; track_index++) {
        for (size_t i = 0; i < data_size; i++) {
            s16* placeToWriteTo = (s16*)&out_buffer[sizeof(WAVHeader) + (track_index * 2) + (i * heading->NumChannels * 2)];
            *placeToWriteTo = data[track_index].data[i];
        }
    }

    out_file.write(out_buffer.data(), outbuffer_size);

    if (heading->LoopStart > 0 && loopcount > 0) {
        char* loopStartPos = out_buffer.data() + sizeof(WAVHeader) + (heading->LoopStart * 2 * heading->NumChannels);
        size_t loopEnd_Size = (heading->LoopEnd - heading->LoopStart) * heading->NumChannels * 2;

        for (u32 i = 0; i < loopcount; i++) {
            out_file.write(loopStartPos, loopEnd_Size);
        }

        if (fadeout_in_samples <= (heading->LoopEnd - heading->LoopStart)) {
            size_t fadeout_size = fadeout_in_samples * heading->NumChannels * 2;
            std::vector<char> fadeout_buffer(fadeout_size);
            memcpy(fadeout_buffer.data(), loopStartPos, fadeout_size);

            for (u32 i = 0; i < fadeout_in_samples; i++) {
                for (u32 track_index = 0; track_index < heading->NumChannels; track_index++) {
                    s16* locToWriteTo = (s16*)&fadeout_buffer[(i * heading->NumChannels * 2) + (track_index * 2)];
                    float fade_percent = (float)(fadeout_in_samples - i) / (float)fadeout_in_samples;
                    *locToWriteTo = (s16)((float)(*locToWriteTo) * fade_percent);
                }
            }
            out_file.write(fadeout_buffer.data(), fadeout_size);
        }
    }

    if (out_file.fail()) {
        std::cout << "ERROR WHILE WRITING TO FILE!\n";
        out_file.close();
        return -1;
    }
    out_file.close();
    return 0;
}

class ASTInfo {
private:
    std::string filename;
    unsigned int customSampleRate = 0;
    unsigned int sampleRate = 0;
    unsigned short numChannels = 0;
    unsigned int numSamples = 0;
    unsigned short isLooped = 65535;
    unsigned int loopStart = 0;
    unsigned int astSize = 0;
    unsigned int wavSize = 0;
    unsigned int blockSize = 10080;
    unsigned int excBlkSz = 0;
    unsigned int numBlocks = 0;
    unsigned int padding = 0;

public:
    int grabInfo(int, char**);
    int getWAVData(FILE*);
    int assignValue(char*, char*);
    int writeAST(FILE*);
    void printHeader(FILE*);
    void printAudio(FILE*, FILE*);
};

int ASTInfo::grabInfo(int argc, char** argv) {
    this->filename = argv[2];

    FILE* sourceWAV = nullptr;
    fopen_s(&sourceWAV, this->filename.c_str(), "rb");
    if (!sourceWAV) {
        printf("ERROR: Cannot find/open input file!\n");
        return 1;
    }

    std::string ext = (this->filename.length() >= 4) ? this->filename.substr(this->filename.length() - 4) : "";
    if (ext != ".wav" && ext != ".wave") {
        printf("ERROR: Source file must be a WAV file!\n");
        fclose(sourceWAV);
        return 1;
    }

    this->filename = this->filename.substr(0, this->filename.length() - 4) + ".ast";

    if (this->getWAVData(sourceWAV) != 0) {
        fclose(sourceWAV);
        return 1;
    }

    for (int count = 3; count < argc; count++) {
        if (argv[count][0] == '-') {
            if (strlen(argv[count]) != 2) return 1;

            if (argc - 1 == count) {
                if (this->assignValue(argv[count], nullptr) != 0) return 1;
            }
            else {
                if (this->assignValue(argv[count], argv[count + 1]) != 0) return 1;
                if (argv[count][1] != 'n') count++;
            }
        }
    }

    int result = this->writeAST(sourceWAV);
    fclose(sourceWAV);
    return result;
}

int ASTInfo::assignValue(char* c1, char* c2) {
    char value = c1[1];

    switch (value) {
    case 'n':
        this->isLooped = 0;
        break;
    case 's':
        this->loopStart = atoi(c2);
        break;
    case 't': {
        uint64_t time = atol(c2);
        long double rounded = ((long double)time / 1000000.0) * (long double)this->sampleRate + 0.5;
        this->loopStart = (int)rounded;
        break;
    }
    case 'e': {
        uint64_t samples = atoi(c2);
        if (samples == 0) {
            printf("ERROR: Total number of samples cannot be zero!\n");
            return 1;
        }
        if (this->numSamples > (unsigned int)samples) this->numSamples = (unsigned int)samples;
        this->wavSize = this->numSamples * 2 * this->numChannels;
        break;
    }
    case 'f': {
        uint64_t time = atol(c2);
        if (time == 0) {
            printf("ERROR: Ending point cannot be zero!\n");
            return 1;
        }
        long double rounded = ((long double)time / 1000000.0) * (long double)this->sampleRate + 0.5;
        uint64_t samples = (uint64_t)rounded;
        if (samples == 0) {
            printf("ERROR: End point is effectively zero!\n");
            return 1;
        }
        if (this->numSamples > (unsigned int)samples) this->numSamples = (unsigned int)samples;
        this->wavSize = this->numSamples * 2 * this->numChannels;
        break;
    }
    case 'r':
        this->customSampleRate = atoi(c2);
        if (this->customSampleRate == 0) this->customSampleRate = this->sampleRate;
        break;
    default:
        return 1;
    }
    return 0;
}

int ASTInfo::getWAVData(FILE* sourceWAV) {
    char riff[5] = { 0 }, wavefmt[5] = { 0 };
    fseek(sourceWAV, 0, SEEK_SET);
    fread(riff, 4, 1, sourceWAV);
    fseek(sourceWAV, 8, SEEK_SET);
    fread(wavefmt, 4, 1, sourceWAV);

    if (strcmp("RIFF", riff) != 0 || strcmp("WAVE", wavefmt) != 0) {
        printf("ERROR: Invalid WAV header!\n");
        return 1;
    }

    unsigned int chunkSZ;
    char fmt[5] = { 0 };
    bool isFmt = false;

    while (fread(fmt, 4, 1, sourceWAV) == 1) {
        if (strcmp("fmt ", fmt) == 0) {
            isFmt = true;
            break;
        }
        if (fread(&chunkSZ, 4, 1, sourceWAV) != 1) break;
        fseek(sourceWAV, chunkSZ, SEEK_CUR);
    }

    if (!isFmt) {
        printf("ERROR: No fmt chunk found!\n");
        return 1;
    }

    unsigned short PCM;
    fseek(sourceWAV, 4, SEEK_CUR);
    fread(&PCM, 2, 1, sourceWAV);

    fread(&this->numChannels, 2, 1, sourceWAV);
    if (this->numChannels < 1 || this->numChannels > 16) {
        printf("ERROR: Invalid number of channels!\n");
        return 1;
    }

    fread(&this->sampleRate, 4, 1, sourceWAV);
    this->customSampleRate = this->sampleRate;

    short bitrate;
    fseek(sourceWAV, 6, SEEK_CUR);
    fread(&bitrate, 2, 1, sourceWAV);

    if (bitrate != 16) {
        printf("ERROR: Only 16-bit PCM is supported!\n");
        return 1;
    }

    char data[5] = { 0 };
    bool isData = false;
    fseek(sourceWAV, 12, SEEK_SET);

    while (fread(data, 4, 1, sourceWAV) == 1) {
        if (strcmp("data", data) == 0) {
            isData = true;
            break;
        }
        if (fread(&chunkSZ, 4, 1, sourceWAV) != 1) break;
        fseek(sourceWAV, chunkSZ, SEEK_CUR);
    }

    if (!isData) {
        printf("ERROR: No data chunk found!\n");
        return 1;
    }

    fread(&this->wavSize, 4, 1, sourceWAV);
    this->numSamples = this->wavSize / (this->numChannels * 2);
    return 0;
}

int ASTInfo::writeAST(FILE* sourceWAV) {
    this->excBlkSz = (this->numSamples * 2) % this->blockSize;
    this->numBlocks = (this->numSamples * 2) / this->blockSize;
    if (this->excBlkSz != 0) this->numBlocks++;

    this->padding = 32 - (this->excBlkSz % 32);
    if (this->padding == 32) this->padding = 0;

    if ((uint64_t)wavSize + (uint64_t)(this->numBlocks * 32) + (uint64_t)(this->padding * this->numChannels) >= 4294967232ULL) {
        printf("ERROR: Input file is too large!\n");
        return 1;
    }

    this->astSize = wavSize + (this->numBlocks * 32) + (this->padding * this->numChannels);

    if (this->filename.length() >= 4) {
        std::string ext = this->filename.substr(this->filename.length() - 4);
        if (_strcmpi(ext.c_str(), ".ast") != 0) this->filename += ".ast";
    }
    else {
        this->filename += ".ast";
    }

    if (this->numBlocks == 0) {
        printf("ERROR: Source WAV contains no audio data!\n");
        return 1;
    }

    if (this->excBlkSz == 0) this->excBlkSz = this->blockSize;
    if (this->loopStart >= this->numSamples) this->loopStart = 0;
    if (this->customSampleRate == 0) {
        printf("ERROR: Invalid sample rate!\n");
        return 1;
    }

    size_t slashPos = this->filename.find_last_of("/\\");
    if (slashPos != std::string::npos) {
        std::string dirPath = this->filename.substr(0, slashPos + 1);
        CreateDirectoryA(dirPath.c_str(), NULL);
    }

    FILE* outputAST = nullptr;
    fopen_s(&outputAST, this->filename.c_str(), "wb");
    if (!outputAST) {
        printf("ERROR: Couldn't create file!\n");
        return 1;
    }

    bool isLoopedFlag = (this->isLooped != 0);
    if (!isLoopedFlag) this->loopStart = 0;

    uint64_t startTime = (uint64_t)((long double)this->loopStart / (long double)this->customSampleRate * 1000000.0 + 0.5);
    uint64_t endTime = (uint64_t)((long double)this->numSamples / (long double)this->customSampleRate * 1000000.0 + 0.5);

    printf("File opened successfully!\n\n\tAST file size: %d bytes\n\tSample rate: %d Hz\n\tIs looped: %s\n",
        this->astSize + 64, this->customSampleRate, isLoopedFlag ? "true" : "false");

    if (isLoopedFlag) {
        printf("\tStarting loop point: %d samples (time: %d:%02d.%06d)\n",
            this->loopStart, (int)(startTime / 60000000), (int)(startTime / 1000000) % 60, (int)(startTime % 1000000));
    }

    printf("\tEnd of stream: %d samples (time: %d:%02d.%06d)\n\tNumber of channels: %d",
        this->numSamples, (int)(endTime / 60000000), (int)(endTime / 1000000) % 60, (int)(endTime % 1000000), this->numChannels);

    if (this->numChannels == 1) printf(" (mono)");
    else if (this->numChannels == 2) printf(" (stereo)");

    printf("\n\nWriting %s...", this->filename.c_str());

    this->printHeader(outputAST);
    this->printAudio(sourceWAV, outputAST);

    printf("...DONE!\n");
    fclose(outputAST);
    return 0;
}

void ASTInfo::printHeader(FILE* outputAST) {
    fwrite("STRM", 4, 1, outputAST);

    uint32_t val = _byteswap_ulong(this->astSize);
    fwrite(&val, 4, 1, outputAST);

    val = 268435712;
    fwrite(&val, 4, 1, outputAST);

    uint16_t shortVal = _byteswap_ushort(this->numChannels);
    fwrite(&shortVal, 2, 1, outputAST);

    fwrite(&this->isLooped, 2, 1, outputAST);

    val = _byteswap_ulong(this->customSampleRate);
    fwrite(&val, 4, 1, outputAST);

    val = _byteswap_ulong(this->numSamples);
    fwrite(&val, 4, 1, outputAST);

    val = _byteswap_ulong(this->loopStart);
    fwrite(&val, 4, 1, outputAST);

    uint32_t loopEnd = this->numSamples;
    val = _byteswap_ulong(loopEnd);
    fwrite(&val, 4, 1, outputAST);

    if (this->numBlocks == 1) val = _byteswap_ulong(this->excBlkSz + this->padding);
    else val = _byteswap_ulong(this->blockSize);
    fwrite(&val, 4, 1, outputAST);

    val = 0;
    fwrite(&val, 4, 1, outputAST);
    val = 127;
    fwrite(&val, 4, 1, outputAST);
    val = 0;
    for (int x = 0; x < 5; ++x) fwrite(&val, 4, 1, outputAST);
}

void ASTInfo::printAudio(FILE* sourceWAV, FILE* outputAST) {
    uint32_t length = this->blockSize * this->numChannels;
    uint32_t paddedLength = this->blockSize;
    unsigned short offset = this->numChannels;

    std::vector<uint16_t> block(this->blockSize * this->numChannels);
    std::vector<uint16_t> printBlock(this->blockSize * this->numChannels);
    const uint64_t headerPad[3] = { 0, 0, 0 };

    for (unsigned int x = 0; x < this->numBlocks; ++x) {
        unsigned int blockIndex = 0;
        fwrite("BLCK", 4, 1, outputAST);

        if (x == this->numBlocks - 1) {
            memset(block.data(), 0, this->blockSize * this->numChannels * 2);
            paddedLength = this->excBlkSz + this->padding;
            length = this->excBlkSz * this->numChannels;
        }

        uint32_t bePaddedLength = _byteswap_ulong(paddedLength);
        fwrite(&bePaddedLength, 4, 1, outputAST);
        fwrite(headerPad, 24, 1, outputAST);

        fread(block.data(), length, 1, sourceWAV);

        for (unsigned int y = 0; y < this->numChannels; ++y) {
            for (unsigned int z = y; z < length / 2; z += offset) {
                printBlock[blockIndex++] = _byteswap_ushort(block[z]);
            }
            if (x == this->numBlocks - 1) {
                for (unsigned int z = 0; z < this->padding; z += 2) {
                    printBlock[blockIndex++] = 0;
                }
            }
        }
        fwrite(printBlock.data(), blockIndex * 2, 1, outputAST);
    }
}

void printUsage() {
    printf("AST Converter Usage:\n");
    printf("  -e input.wav [options]  : Encode WAV to AST\n");
    printf("  -d input.ast [loopcount] : Decode AST to WAV\n");
    printf("\nEncode options:\n");
    printf("  -s [samples]      : Loop start in samples\n");
    printf("  -t [microseconds] : Loop start in microseconds\n");
    printf("  -n                : Disable looping\n");
    printf("  -e [samples]      : End point in samples\n");
    printf("  -f [microseconds] : End point in microseconds\n");
    printf("  -r [rate]         : Sample rate override\n");
    printf("\nDecode:\n");
    printf("  -d input.ast [loopcount] : Decode with optional loop count\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "-d") {
        const char* filename = argv[2];
        u32 loopcount = (argc > 3) ? std::stoi(argv[3]) : 1;

        std::string outputname = filename;
        size_t dotpos = outputname.find_last_of('.');
        if (dotpos != std::string::npos && outputname.substr(dotpos) == ".ast") {
            outputname = outputname.substr(0, dotpos);
        }
        outputname += ".wav";

        return convertASTtoWAV(filename, outputname.c_str(), loopcount);
    }
    else if (mode == "-e") {
        ASTInfo createFile;
        return createFile.grabInfo(argc, argv);
    }
    else {
        printf("Unknown mode: %s\n", mode.c_str());
        printUsage();
        return 1;
    }
}