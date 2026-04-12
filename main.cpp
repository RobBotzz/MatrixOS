#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

int main()
{
    double value = 10;
    cout << "Your value: " << value;

    cout << "\nWaiting for 7 seconds before closing...";
    this_thread::sleep_for(chrono::seconds(5));
}