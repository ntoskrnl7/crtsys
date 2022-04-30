#include "setlocal.h"

__lc_time_data __lc_time_c = {
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},

    {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
     "Saturday"},

    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
     "Nov", "Dec"},

    {"January", "February", "March", "April", "May", "June", "July", "August",
     "September", "October", "November", "December"},

    {"AM", "PM"},

    "MM/dd/yy",
    "dddd, MMMM dd, yyyy",
    "HH:mm:ss",

    1,
    0,

    {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"},

    {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday",
     L"Saturday"},

    {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep",
     L"Oct", L"Nov", L"Dec"},

    {L"January", L"February", L"March", L"April", L"May", L"June", L"July",
     L"August", L"September", L"October", L"November", L"December"},

    {L"AM", L"PM"},

    L"MM/dd/yy",
    L"dddd, MMMM dd, yyyy",
    L"HH:mm:ss",
    L"en-US"};

struct __lc_time_data *__lc_time_curr = &__lc_time_c;
