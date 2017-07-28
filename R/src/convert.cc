// Convert between an Arrow array and an R vector

Rdouble_to_arrow


Status PandasObjectsToArrow(MemoryPool* pool, PyObject* ao, PyObject* mo,
    const std::shared_ptr<DataType>& type, std::shared_ptr<ChunkedArray>* out) {
  PandasConverter converter(pool, ao, mo, type);
  RETURN_NOT_OK(converter.ConvertObjects());
  *out = std::make_shared<ChunkedArray>(converter.result());
  return Status::OK();
}
