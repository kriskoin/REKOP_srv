/****************************************************/
/*                                                  */
/*      Quick Sort Module                           */
/*      by Mike Benna                               */
/*      file created April 88                       */
/*      Copyright (C) 1988 Mike Benna               */
/*                                                  */
/****************************************************/
/*
    Notes on the way the quicksort routine works:

    The quicksort routine is a very generic sort routine
    because it uses user supplied compare and swap functions.
    The format for calling is:

        QSort (int SizeOfList, int (*CompareFunc)(), void (*SwapFunc)());

        The format for the Compare and Swap functions is:

            Compare (int n1, int n2);
            Swap    (int n1, int n2);

        The parms passed are the indexes (0..SizeOfList) of the entry
        that the QSort routine wants tested.

        At present I don't remember what the return value of the Compare
        function is supposed to be but a good guess would be the same
        as the MSC library routine qsort(). If somebody bothers to figure
        it out, I'd appreciate getting the info so that I could put it
        here in the documentation.
*/

#include "pplib.h"

static int iQSortDepth;
int iQSortMaxDepth;

static int Split(int low, int high, int (*comparefunc)(int,int,void *), void (*swapfunc)(int,int,void *), void *base)
{
    int left = low;
    int right = high;
    int mid = (high-low)/2 + low;
    if (low!=mid)
        (*swapfunc)(low, mid, base);

    while (left < right) {
        while ((*comparefunc)(low, right, base) < 0)
            right--;
        while ((left < right) && ((*comparefunc)(low, left, base) >= 0))
            left++;
        if (left < right)
            (*swapfunc)(left, right, base);
    }
    if (right!=low)
        (*swapfunc)(right, low, base);
    return right;
}

static void QuickSort(int low, int high, int (*comparefunc)(int,int,void *), void (*swapfunc)(int,int,void *), void *base)
{
	iQSortDepth++;
	if (iQSortDepth > iQSortMaxDepth) {
		iQSortMaxDepth = iQSortDepth;
	}
    if (low < high) {       /* list has more than one item */
        int mid = Split(low, high, comparefunc, swapfunc,base);
        QuickSort(low, mid-1, comparefunc, swapfunc,base);
        QuickSort(mid+1, high, comparefunc, swapfunc,base);
    }
	iQSortDepth--;
}

void QSort(int n, int (*comparefunc)(int,int,void *), void (*swapfunc)(int,int,void *), void *base)
{
	iQSortDepth = iQSortMaxDepth = 0;	// reset record keeping vars
    QuickSort(0, n-1, comparefunc, swapfunc, base);             /* do the sort! */
}
