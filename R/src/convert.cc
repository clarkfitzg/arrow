// Convert between an Arrow array and an R vector

#include "Rarrow.h"

// From Rdefines - Got to be a better way
#undef length

using namespace arrow;


/* Example from Rrawpoppler
 *
extern "C"
SEXP R_Catalog_getPage(SEXP r_tthis, SEXP r_i)
{
    Catalog *tthis = GET_REF(r_tthis, Catalog);
    int i;
    i = INTEGER(r_i)[0];
    Page * ans;
    ans = tthis->getPage(i);
    SEXP r_ans = R_createRef(ans, "PagePtr", NULL);
    return(r_ans);
}
*/

// Just trying to get it to work
//DoubleArray* arr;


extern "C"
SEXP R_double_to_arrow(SEXP x)
// Return a pointer to a newly created Arrow array
{

    // C pointers to the actual data
    double *xr = REAL(x);
    int n = Rf_length(x);

    // Wes' code
    //
    // Declare a new shared_ptr to a MutableBuffer
    std::shared_ptr<MutableBuffer> data_buf;
    // Allocate buffer, maintaining 64 byte padding
    AllocateBuffer(default_memory_pool(), n * sizeof(double), &data_buf);
    // Physically copy the data
    memcpy(data_buf->mutable_data(), xr, n * sizeof(double));

    //std::shared_ptr<DoubleArray> arr = std::make_shared<DoubleArray>(n, data_buf);
    // Should be equivalent
    //std::shared_ptr<DoubleArray> arr(new DoubleArray(n, data_buf));
    
    DoubleArray* arr;
    // TODO: Not deleted => leaks memory
    arr = new DoubleArray(n, data_buf);
    
    //arr.reset(new DoubleArray(n, data_buf));

    // TODO: Pass destructor for array?
    SEXP r_ans = R_createRef(arr, "arrow.array", NULL);
    // arr.get() returns the address for the DoubleArray. Then this
    // pointer should become invalid because when arr goes out of scope the
    // DoubleArray inside should be deleted
    //
    // I think what we really need is to be able to pass a shared_ptr into
    // R_createRef here
    return(r_ans);
}


// TODO: This is currently broken.
extern "C"
SEXP R_arrow_to_double(SEXP x)
{
    // Follow the pointer in the R object to the C object
    std::shared_ptr<DoubleArray>* array = GET_REF(x, std::shared_ptr<DoubleArray>);

	// Get the pointer to the actual data
	const double* data = (*array)->raw_values();

    int n = (*array)->length();

    SEXP out = PROTECT(allocVector(REALSXP, n));

    // C pointers to the actual data
    double* outp = REAL(out);

    // Using C std library to copy memory, Arrow may provide better way
    memcpy(outp, data, n * sizeof(double));

    UNPROTECT(1);
    return out;
}

