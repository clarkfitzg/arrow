
## Open C++ Questions

Given an array of doubles in C (no null values), what's the simplest way to make an
`arrow::Array` object from them? The `builder.append()` approach as
described in the [C++ docs](https://arrow.apache.org/docs/cpp/index.html)
seems more appropriate if we don't know the initial size of the array.

------------------------------------------------------------

Where is the `Int64Array` defined in Arrow as used in the line:

```
std::shared_ptr<Int64Array> int64_array = std::static_pointer_cast<Int64Array>(array);
```

Because I'd like to do the same with an array of doubles.

------------------------------------------------------------

Line 294 of `pandas_to_arrow.cc` calls the conversion:

>   // Traditional visitor conversion for non-object arrays

Is there a reference for this? I looked through the `visitor` files source
and am not sure what's happening.


## First Steps

Fri Jul 28 14:24:14 PDT 2017

Following Wes' note on the mailing list.

The first goal is go back and forth between R's `data.frame` and arrow
record batches.

Python currently implements one direction for this in the C++
`arrow_to_pandas.h`:

```
Status ConvertArrayToPandas(
    const std::shared_ptr<Array>& arr, PyObject* py_ref, PyObject** out);

Status ConvertColumnToPandas(
    const std::shared_ptr<Column>& col, PyObject* py_ref, PyObject** out);

Status ConvertTableToPandas(
    const std::shared_ptr<Table>& table, int nthreads, PyObject** out);
```

Following this, the simplest thing to do is convert between Arrow arrays
and
R vectors for a couple numeric types.

Appears that `PushArray` on line 333 in `pandas_to_arrow.h` is doing the
actual work.


## Feather

Scanning through the feather code base, they write S3 methods for the file
containing the feather data to emulate a data.frame on disk. Perfectly
reasonable.


## What I want

- Better interoperability between R and other languages / big data tools
- A file format that supports chunked computations in a more natural way.
  Hopefully this will enhance parallel and out of core computing.
- Efficient columnar storage that preserves metadata
- Learn more C++ / low level details of data storage
