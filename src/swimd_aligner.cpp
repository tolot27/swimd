#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <ctime>
#include <string>
#include <climits>

#include "Swimd.h"
#include "ScoreMatrix.hpp"

using namespace std;

bool readFastaSequences(FILE* &file, char* alphabet, int alphabetLength, vector< vector<unsigned char> >* seqs);

int main(int argc, char * const argv[]) {
    int gapOpen = 3;
    int gapExt = 1;
    ScoreMatrix scoreMatrix;
    
    //----------------------------- PARSE COMMAND LINE ------------------------//
    string scoreMatrixName = "Blosum50";
    bool scoreMatrixFileGiven = false;
    char scoreMatrixFilepath[512];
    bool silent = false;
    char mode[16] = "SW";
    int option;
    while ((option = getopt(argc, argv, "a:o:e:m:f:s")) >= 0) {
        switch (option) {
        case 'a': strcpy(mode, optarg); break;
        case 'o': gapOpen = atoi(optarg); break;
        case 'e': gapExt = atoi(optarg); break;
        case 'm': scoreMatrixName = string(optarg); break;
        case 'f': scoreMatrixFileGiven = true; strcpy(scoreMatrixFilepath, optarg); break;
        case 's': silent = true; break;
        }
    }
    if (optind + 2 != argc) {
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: swimd_aligner [options...] <query.fasta> <db.fasta>\n");        
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "\t-g N  N is gap opening penalty. [default: 3]\n");
        fprintf(stderr, "\t-e N  N is gap extension penalty. [default: 1]\n");
        fprintf(stderr, "\t-m Blosum50  Score matrix to be used. [default: Blosum50]\n"); 
        fprintf(stderr, "\t-f FILE  FILE contains score matrix and some additional data. Overrides -m.\n");
        fprintf(stderr, "\t-s  If specified, there will be no score output (silent mode).\n");
        fprintf(stderr, "\t-a SW|NW|HW|OV  Alignment mode that will be used. [default: SW]\n");
        return 1;
    }
    //-------------------------------------------------------------------------//

    // Set score matrix by name
    if (scoreMatrixName == "Blosum50") 
        scoreMatrix = ScoreMatrix::getBlosum50();
    else {
        fprintf(stderr, "Given score matrix name is not valid\n");
        exit(1);
    }
    // Set score matrix by filepath
    if (scoreMatrixFileGiven) {
        scoreMatrix = ScoreMatrix(scoreMatrixFilepath);
    }

    char* alphabet = scoreMatrix.getAlphabet();
    int alphabetLength = scoreMatrix.getAlphabetLength();


    // Detect mode
    int modeCode;
    if (!strcmp(mode, "SW"))
        modeCode = SWIMD_MODE_SW;
    else if (!strcmp(mode, "HW"))
        modeCode = SWIMD_MODE_HW;
    else if (!strcmp(mode, "NW"))
        modeCode = SWIMD_MODE_NW;
    else if (!strcmp(mode, "OV"))
        modeCode = SWIMD_MODE_OV;
    else {
        printf("Invalid mode!\n");
        return 1;
    }
    printf("Using %s alignment mode.\n", mode);

    int readResult;
    // Build query
    char* queryFilepath = argv[optind];
    FILE* queryFile = fopen(queryFilepath, "r");
    if (queryFile == 0) {
        printf("Error: There is no file with name %s\n", queryFilepath);
        return 1;
    }
    vector< vector<unsigned char> >* querySequences = new vector< vector<unsigned char> >();
    printf("Reading (multiple) query fasta file...\n");
    bool wholeQueryRead = readFastaSequences(queryFile, alphabet, alphabetLength, querySequences);
    if (wholeQueryRead) {
        printf("Whole query file (%d sequence(s)) read!\n", (int)querySequences->size());
    } else {
        printf("Chunk of query file (%d sequence(s)) read!\n", (int)querySequences->size());
    }

    // Build db
    char* dbFilepath = argv[optind+1];
    FILE* dbFile = fopen(dbFilepath, "r");
    int wholeDbLength = 0;
    if (dbFile == 0) {
        printf("Error: There is no file with name %s\n", dbFilepath);
        return 1;
    }

    double cpuTime = 0;
    bool wholeDbRead = false;
    while (!wholeDbRead) {
        vector< vector<unsigned char> >* dbSequences = new vector< vector<unsigned char> >();
        printf("\nReading database fasta file...\n");
        wholeDbRead = readFastaSequences(dbFile, alphabet, alphabetLength, dbSequences);
        if (wholeDbRead) {
            printf("Whole database read!\n");
        } else {
            printf("Chunk of database read!\n");
        }
    
        int dbLength = dbSequences->size();
        unsigned char** db = new unsigned char*[dbLength];
        int* dbSeqLengths = new int[dbLength];
        int dbNumResidues = 0;
        for (int i = 0; i < dbSequences->size(); i++) {
            db[i] = (*dbSequences)[i].data();
            dbSeqLengths[i] = (*dbSequences)[i].size();
            dbNumResidues += dbSeqLengths[i];
        }
        printf("Read %d database sequences, %d residues total.\n", dbLength, dbNumResidues);

        // ----------------------------- MAIN CALCULATION ----------------------------- //
        for (unsigned int j = 0; j < querySequences->size(); j++) {
        unsigned char* query = (*querySequences)[j].data();
        int queryLength = (*querySequences)[j].size();
        printf("Search query sequence, %d residues.\n", queryLength);
        
        int* scores = new int[dbLength];    
        printf("\nComparing query to database...");
        fflush(stdout);
        clock_t start = clock();
        int resultCode = swimdSearchDatabase(query, queryLength, db, dbLength, dbSeqLengths,
                                             gapOpen, gapExt, scoreMatrix.getMatrix(), alphabetLength,
                                             scores, modeCode);
        clock_t finish = clock();
        cpuTime += ((double)(finish-start))/CLOCKS_PER_SEC;
        // ---------------------------------------------------------------------------- //
        printf("\nFinished!\n");
    
        if (!silent) {
            printf("\nScores (<database sequence number>: <score>): \n");
            for (int i = 0; i < dbLength; i++)
                printf("#%d: %d\n", wholeDbLength + i, scores[i]);
        }

        delete[] scores;
        }
        wholeDbLength += dbLength;
        
        delete[] db;
        delete[] dbSeqLengths;
        delete dbSequences;
    }

    printf("\nSearch of all query sequences against database completed!\n");
    printf("\nCpu time of searching: %lf\n", cpuTime);
    printf("Read %d database sequences in total.\n", wholeDbLength);

    // Print this statistics only for SW because they are not valid for other modes.
    /*   if (!(strcmp(mode, "SW"))) {
        int for8, for16, for32;
        for8 = for16 = for32 = 0;
        double averageScore = 0;
        for (int i = 0; i < dbLength; i++) {
            averageScore += (double)scores[i] / dbLength;
            if (scores[i] < CHAR_MAX)
                for8++;
            else if (scores[i] < SHRT_MAX)
                for16++;
            else
                for32++;
        }
        printf("\nDatabase statistics:\n");
        printf("\tFor 8  (< %10d): %8d\n", CHAR_MAX, for8);
        printf("\tFor 16 (< %10d): %8d\n", SHRT_MAX, for16);
        printf("\tFor 32 (< %10d): %8d\n",  INT_MAX, for32);
        printf("\tAverage score: %lf\n", averageScore);
        }*/

    fclose(queryFile);
    fclose(dbFile);
    // Free allocated space
    delete querySequences;
    
    return 0;
}




/** Reads sequences from fasta file. If it reads more than some amount of sequences, it will stop.
 * @param [in] file File pointer to database. It may not be the beginning of database.
 * @param [in] alphabet
 * @param [in] alphabetLength
 * @param [out] seqs Sequences will be stored here, each sequence as vector of indexes from alphabet.
 * @return true if reached end of file, otherwise false.
 */
bool readFastaSequences(FILE* &file, char* alphabet, int alphabetLength, vector< vector<unsigned char> >* seqs) {
    seqs->clear();

    unsigned char letterIdx[128]; //!< letterIdx[c] is index of letter c in alphabet
    for (int i = 0; i < alphabetLength; i++)
        if (alphabet[i] == '*') { // '*' represents all characters not in alphabet
            for (int j = 0; j < 128; j++)
                letterIdx[j] = i;
            break;
        }
    for (int i = 0; i < alphabetLength; i++)
        letterIdx[alphabet[i]] = i;

    long numResiduesRead = 0;
    bool inHeader = false;
    bool inSequence = false;
    int buffSize = 4096;
    char buffer[buffSize];
    while (!feof(file)) {
        int read = fread(buffer, sizeof(char), buffSize, file);
        for (int i = 0; i < read; ++i) {
            char c = buffer[i];
            if (inHeader) { // I do nothing if in header
                if (c == '\n')
                    inHeader = false;
            } else {
                if (c == '>') {
                    inHeader = true;
                    inSequence = false;
                } else {
                    if (c == '\r' || c == '\n')
                        continue;
                    // If starting new sequence, initialize it.
                    // Before that, check if we read more than 1GB of sequences,
                    // and if that is true, finish reading.
                    if (inSequence == false) {
                        if (seqs->size() > 0) {
                            numResiduesRead += seqs->back().size();
                        }
                        if (numResiduesRead > 1073741824L) {
                            fseek(file, i - read, SEEK_CUR);
                            return false;
                        }
                        inSequence = true;
                        seqs->push_back(vector<unsigned char>());
                    }

                    seqs->back().push_back(letterIdx[c]);
                }
            }
        }
    }

    return true;
}
 

