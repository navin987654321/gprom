#!/bin/bash
./test/testserializer -host fourier.cs.iit.edu -db orcl -port 1521 -user tprov -passwd "XA<w67onz" -sql "PROVENANCE AS OF SCN 1234 OF (SELECT b FROM r;);" -loglevel 3
