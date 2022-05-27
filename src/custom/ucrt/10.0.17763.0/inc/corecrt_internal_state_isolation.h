#pragma once

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

    inline int get_current_state_index(__crt_scoped_get_last_error_reset const &last_error_reset)
    {
        last_error_reset;
        return 0;
    }

    template <typename T> class dual_state_global
    {
      public:
        T &value()
        {
            return _value[0];
        }

        void initialize(const T &value)
        {
            for (size_t i = 0; i != state_index_count; i++)
                _value[i] = value;
        }

        template <typename Ta> void initialize_from_array(Ta (&arr)[2])
        {
            for (size_t i = 0; i != state_index_count; i++)
                _value[i] = arr[i];
        }

        template <class Fn> void uninitialize(Fn &&fn)
        {
            for (size_t i = 0; i != state_index_count; i++)
                fn(_value[i]);
        }

        T *const dangerous_get_state_array()
        {
            return reinterpret_cast<T *const>(&_value);
        }

        T _value[state_index_count];
    };
    } // namespace __crt_state_management
}
#endif