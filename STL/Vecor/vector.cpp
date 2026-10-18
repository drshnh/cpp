#include <iostream>
#include <iterator>
#include <string>

template<typename T>
class vector{
using iterator  = T*;
    T* ptr{nullptr};
    size_t size{0};
    size_t capacity {0};

    void reallocate(size_t new_size){
        T* new_ptr = static_cast<T*>(::operator new(new_size*sizeof(T)));
        
        if constexpr(std::is_move_constructible_v<T>){
            for(auto i=0; i< size; ++i){
                ::new (&new_ptr[i]) T(std::move(ptr[i]));
                ptr[i].~T(); 
            }
        }else{
            for(auto i=0; i< size; ++i){
                ::new (&new_ptr[i]) T((ptr[i]));
                ptr[i].~T(); 
            }
        }
        ::operator delete(ptr);
        ptr = new_ptr;
        capacity = new_size;
    }

    void reserve(size_t new_size){
        if(new_size > capacity){
            reallocate(new_size);
        }
    }

    public:
    vector() noexcept = default;
    explicit vector(size_t count) {
        std::cout<<"explicit vector() "<<std::endl;
        reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ::new (&ptr[i]) T(); // Default-construct objects in-place
        }
        size = count;
    }
    vector(const vector& other) {
        std::cout<<"vector() "<<std::endl;
        reserve(other.size);
        for (size_t i = 0; i < other.size; ++i) {
            ::new (&ptr[i]) T(other.ptr[i]);
        }
        size = other.size;
    }
    //copy assignment operator
    vector<T>& operator=(const vector<T>& other){
        for(auto it = this->begin(); it != this->end(); it++){
            it->~T();
        }
        this->size = other.size;
        reallocate(other.capacity);
        for(auto i = 0; i< other.size; ++i){
           ::new (&ptr[i]) T(other.ptr[i]);
        }
        
    } 

    iterator begin(){return ptr;}
    iterator end(){return ptr+size;}
    T& front(void )noexcept {return ptr[0];}
    T& back(void )noexcept {return ptr[size-1];}
    bool empty() const noexcept { return size == 0; }
    size_t Size() const noexcept { return size; }
    size_t Capacity() const noexcept { return capacity; }

    void push_back(const T& value){
         if (size >= capacity) {
            reserve(capacity == 0 ? 1 : capacity * 2);
        }
        ::new (&ptr[size]) T(std::move(value)); // Move-construct in-place
        ++size;
    }

    void push_back(T&& value) {
        if (size >= capacity) {
            reserve(capacity == 0 ? 1 : capacity * 2);
        }
        ::new (&ptr[size]) T(std::move(value)); // Move-construct in-place
        ++size;
    }

    void pop_back(void){
        if(size>0){
            ptr[size-1].~T();
            size--;
        }
    }

    T at(size_t num){
        if(num <= (size+1) && (num >0)){
            return ptr[--num];
        }else{
            throw std::out_of_range("Vector index out of bounds!");
        }
    }
};
