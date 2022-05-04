#pragma once

#include <type_traits>

#define __acrt_select_exit_lock() __acrt_exit_lock

#pragma warning(disable : 4101)
#ifdef __cplusplus

extern "C++"
{
    namespace __crt_state_management
    {
    struct scoped_global_state_reset
    {
    };

    const size_t state_index_count = 2;

    inline bool initialize_global_state_isolation()
    {
        return true;
    }

    inline void uninitialize_global_state_isolation(bool const terminating)
    {
        terminating;
    }

    inline int get_current_state_index()
    {
        return 0;
    }

    inline int get_current_state_index(const __crt_scoped_get_last_error_reset &)
    {
        return 0;
    }

    template <typename T> class dual_state_global
    {
      public:
        T &value()
        {
            return values_[0];
        }

        void initialize(const T &value)
        {
            for (size_t i = 0; i != state_index_count; i++)
            {
                values_[i] = value;
            }
        }

        template <class Tx> void initialize_from_array(const Tx &arr)
        {
            KdBreakPoint();            
            memcpy(&values_, (void *)&arr, sizeof(values_));
        }

        template <class Fn> void uninitialize(Fn &&)
        {
            KdBreakPoint();
        }

        T *const dangerous_get_state_array()
        {
            return reinterpret_cast<T *const>(&values_);
        }

        T values_[state_index_count];
    };
    } // namespace __crt_state_management
}
#endif