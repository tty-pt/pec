pec: pec.c
	${LINK.c} -g -o $@ pec.c -lit -lqhash -ldb
