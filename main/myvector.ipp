
template <typename T>
myvector<T>::myvector() {}

template <typename T>
myvector<T>::myvector(T *p_begin, T *p_end)
{
   std::ptrdiff_t len = p_end - p_begin;
   if (len <= 0)
      return;
   data = alloc_v(static_cast<std::size_t>(len));
   if (!begin())
      return;
   size = len;
   capacity = len;
   std::copy(p_begin, p_end, begin());
}

template <typename T>
myvector<T>::myvector(std::initializer_list<T> list)
{
   std::ptrdiff_t len = list.end() - list.begin();
   if (len <= 0)
      return;
   data = alloc_v(static_cast<std::size_t>(len));
   if (!begin())
      return;
   size = len;
   capacity = len;
   std::copy(list.begin(), list.end(), begin());
}

template <typename T>
myvector<T>::myvector(T val, std::size_t count)
{
   if (!reserve(count))
      return;
   size = count;
   capacity = count;
   std::fill(begin(), end(), val);
}

template <typename T>
myvector<T>::myvector(myvector<T> &vec)
{
   data = std::move(vec.data);
   size = vec.size;
   capacity = vec.capacity;

   vec.capacity = 0;
   vec.size = 0;
}

template <typename T>
myvector<T> &myvector<T>::operator=(myvector<T> &vec) noexcept
{
   std::unique_ptr<T[]> ptr = alloc_v(vec.size);
   if (!ptr)
      return *this;
   std::copy(vec.begin(), vec.end(), ptr.get());
   data = std::move(ptr);
   size = vec.size;
   capacity = size;
   return *this;
}

template <typename T>
myvector<T> &myvector<T>::operator=(myvector<T> &&vec) noexcept
{
   data = std::move(vec.data);
   capacity = vec.capacity;
   size = vec.size;
   vec.size = 0;
   vec.capacity = 0;
   return *this;
}

template <typename T>
bool myvector<T>::operator==(const myvector<T> &vec)
{
   return (vec.size == size && std::equal(begin(), end(), vec.begin()));
}

template <typename T>
T &myvector<T>::operator[](std::size_t id)
{
   return data[id];
}

template <typename T>
T *myvector<T>::begin()
{
   return data.get();
}

template <typename T>
T *myvector<T>::end()
{
   return data.get() + size;
}

template <typename T>
void myvector<T>::push_back(T val)
{
   if (size + 1 > capacity)
   {
      std::unique_ptr<T[]> ptr = alloc_v(capacity + CAPACITY_RESIZE);
      if (!ptr)
         return;
      capacity += CAPACITY_RESIZE; // policy agressive towards saving memory
      std::copy(begin(), end(), ptr.get());
      data = std::move(ptr);
   }
   data[size++] = val;
}

template <typename T>
void myvector<T>::shrink_to_fit()
{
   std::unique_ptr<T[]> newptr = alloc_v(size);
   if (!newptr)
      return;
   std::copy(begin(), end(), newptr.get());
   data = std::move(newptr);
   capacity = size;
}

template <typename T>
bool myvector<T>::reserve(std::size_t cap)
{
   std::unique_ptr<T[]> ptr = alloc_v(cap);
   if (!ptr)
      return false;
   if (size > 0)
   {
      std::move(begin(), end(), ptr.get());
   }
   data = std::move(ptr);
   capacity = cap;
   return true;
}

template <typename T>
void myvector<T>::fill(T val)
{
   size = capacity;
   std::fill(begin(), end(), val);
}

template <typename T>
bool myvector<T>::equal(T *ptr, std::size_t len)
{
   return (size == len && std::equal(ptr, ptr + len, data.get()));
}

template <typename T>
bool myvector<T>::insert(std::size_t pos, T val)
{
   if (pos >= size)
      return false;
   if (size + 1 > capacity)
   {
      std::unique_ptr<T[]> ptr = alloc_v(capacity + CAPACITY_RESIZE);
      if (!ptr)
         return false;
      capacity += CAPACITY_RESIZE;
      std::copy(begin(), &data[pos], ptr.get());
      ptr[pos] = val;
      std::copy(&data[pos], end(), ptr.get() + pos + 1);

      data = std::move(ptr);
   }
   else
   {
      T *tomove = &data[pos];
      std::copy_backward(tomove, &data[0] + size, &data[0] + size + 1);
      data[pos] = val;
   }
   size++;
   return true;
}