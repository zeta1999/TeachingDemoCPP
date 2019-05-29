/**
 * This is a very simple example of a lockless concurrent stack, which is of
 * probably very suboptimal performance (no backoffs...).
 * And this is an illustration of how comparatively tricky lockless algorithms
 * can be even in very simple cases, as opposed to a single thread algorithm
 * protected by a lock.
 * Random notes on the lock-free version:
 * - Very few specialized debugging tools (to the best of our knowledge) as
 *  opposed to lock-based threading sanitizers (Valgrind, gcc/llvm thread 
 *  sanitizer, ...).
 * - Code might work on a given machine architecture (say, x86) but fail on
 *  another due to nuances in lockless implementations (esp. strong vs. weak
 *  acquire/release of atomic item). The technicalities are unimportant at 
 *  this stage but please remember: ALWAYS TEST WITH A TARGET THAT IS 
 *  REPRESENTATIVE OF PRODUCTION SETUP...
 * Code will be reviewed again on week 3!
 */

#include <atomic>
#include <vector>
#include <iostream>
#include <cassert>
#include <cstdint>
#include <mutex>

/** Non-thread-safe stack: Last In First Out */
template<typename T>
class stack {
  std::vector<T> contents;
  T *_contents = { nullptr };
  /** Are we sure int32 indexes are enough btw? */
  uint32_t index = { 0 };
  uint32_t size= { 0 };

public:
  inline stack() = default;
  inline void resize(size_t n) {
    contents.resize(n);
    _contents = &contents[0];
  }
  /** Add a COPY of t to the stack */
  inline void push(const T &t) {
    assert(_contents != nullptr);
    assert(index < size-1);
    _contents[index++] = t;
  }
  /** Try to get a COPY of the last inserted 
   element in the stack, returns true on success
   or false on failre (stack empty) */
  inline bool try_pop(T &t) {
    if (index == 0) return false;
    assert(index > 0); // not strictly necessary but...
    t = _contents[--index];
    return true;
  }
};

/** Lock-based thread-safe stack
 * $$$ THIS WILL BE STUDIED IN DETAIL ON WEEK 3 $$$
 */
template<typename T>
class lstack {
  /** We wrap concurrent accesses... */
  stack<T> wrapped;
  /** With a lock guard on one mutex per stack... */
  std::mutex mx;
public:
  inline lstack() = default;
  
  inline void nonconcurrent_resize(size_t n) {
    wrapped.resize(n);
  }
  /** Add a COPY of t to the stack */
  inline void push(const T&t) {
    // only one thread executes this code block at one time (per stack object)
    std::lock_guard<std::mutex> lck(mx);
    wrapped.push(t);
  }
  /** Try to get a COPY of the last inserted 
   element in the stack, returns true on success
   or false on failre (stack empty) */
  inline bool try_pop(T &t) {
    // only one thread executes this code block at one time (per stack object)
    std::lock_guard<std::mutex> lck(mx);
    return wrapped.try_pop(t);
  }
};

/** Very lazy design of a lockfree stack. 
 * N.B. this design has many flaws and is intended to have these flaws, for pedagogic purpose!
 * $$$ THIS WILL STUDIED IN DETAIL ON WEEK 3 $$$
 */
template<typename T>
class lfstack {
  /** Same as non-concurrent */
  std::vector<T> contents;
  /** Same as non-concurrent */
  T *_contents = { nullptr };
  /** Note the index is now atomic */
  std::atomic<uint32_t> index;
  /** Same as non-concurrent */
  uint32_t size= { 0 };

  /** 'index' is in fact index << 1 + write 'lock' flag
   * clean operation retrieves the index without the write flag i.e. index << 1
   */
  inline uint32_t clean_index(uint32_t i) {
    return i & 0xfffffffe;
  }

  /**
   * add 'lock' flag
   */
  inline uint32_t flag_index(uint32_t i) {
    return i | 0x01;
  }
  
  /**
   * get the actual index part
   */
  inline uint32_t read_index(uint32_t i) {
    return i >> 1;
  }

  /**
   * Decrement actual index part, keep write lock flag as is.
   */
  inline uint32_t decrement_index(uint32_t i) {
    assert(i > 0);
    return i-2;
  }

  /**
   * Return true if actual index part is zero, irrespective of write 'lock' flag
   */
  inline bool is_zero(uint32_t i) {
    return i < 2; // n.b. accepts both 0 & 0x1 as 'zero'
  }

  /** Lowest bit is W flag: CAS loop to get exclusive write lock/flag */
  inline uint32_t acquire_write_rights() {
    uint32_t index_copy;
    do {
      index_copy = index.load();
    } while(!std::atomic_compare_exchange_strong(&index,
						 &index_copy,
						 flag_index(index_copy)));
  }

  /**
   * Increment actual index part, keep write lock/flag as is
   */
  inline uint32_t increment_index(uint32_t i) {
    return i+2;
  }
  
public:
  inline lfstack() = default;
  /** Do NOT call concurrently */
  inline void nonconcurrent_resize(size_t n) {
    contents.resize(n);
    _contents = &contents[0];
    size = n;
    index.store(0);
  }

  /** Add a COPY of t to the stack */
  inline void push(const T& t) {
    assert(_contents != nullptr);
    acquire_write_rights();
    // only one thread here ~
    // (write role)
    uint32_t index_copy = 0;
    do {
      index_copy = index.load();
      assert(read_index(index_copy) < size - 1);
	_contents[read_index(clean_index(index_copy))] = t;
      // incremented index has write flag set to ZERO
    } while(!std::atomic_compare_exchange_strong(&index,
					  &index_copy,
						 increment_index(clean_index(index_copy))));
  }

  inline bool try_pop(T &dest) {
    assert(_contents != nullptr);
    uint32_t index_copy = 0;
    do {
      index_copy = clean_index(index.load());
      if (is_zero(index_copy)) return false;
      dest = _contents[read_index(decrement_index(index_copy))];
    } while(!std::atomic_compare_exchange_strong(&index, &index_copy,
				  decrement_index(index_copy)));
    return true;
  }
};

int main() {
  // A possible exercise here is to define more tests and actually test
  // for concurrency performances (both throughput nad latency distribution)
  // More on this in the next weeks of the course.
  // Also, a real production code base would likely have proper unit tests
  // again, this is here a trade-off to expose our main tutorial points
  // instead
  
  // base test, base stack
  {
    stack<int> st;
    st.resize(100);
    st.push(1);
    st.push(2);
    st.push(3);
    int r = -1;
    assert(st.try_pop(r));
    assert(r == 3);
    assert(st.try_pop(r));
    assert(r == 2);
    assert(st.try_pop(r));
    assert(r == 1);

    std::cout << "stack ok" << std::endl << std::flush;
  }

  // base test, lock based concurrent stack
  {
    lstack<int> st;
    st.nonconcurrent_resize(100);
    st.push(1);
    st.push(2);
    st.push(3);
    int r = -1;
    assert(st.try_pop(r));
    assert(r == 3);
    assert(st.try_pop(r));
    assert(r == 2);
    assert(st.try_pop(r));
    assert(r == 1);

    std::cout << "lstack ok" << std::endl << std::flush;
  }
  
  // base test, lock-free stack
  {
    lfstack<int> st;
    st.nonconcurrent_resize(100);
    st.push(1);
    st.push(2);
    st.push(3);
    int r = -1;
    assert(st.try_pop(r));
    assert(r == 3);
    assert(st.try_pop(r));
    assert(r == 2);
    assert(st.try_pop(r));
    assert(r == 1);

    std::cout << "lfstack ok" << std::endl << std::flush;
  }

  // as you can see we did not test for multithreaded usage
  // THIS IS AN ABSOLUTE NO-NO FOR PRODUCTION RATE CODE
  // $$$ More about this in the following weeks $$$
}
