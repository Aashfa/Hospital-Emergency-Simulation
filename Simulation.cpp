#include <iostream>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <ctime>
#include <memory>

using namespace std;

// Priority Levels
enum Priority { HIGH, MEDIUM, LOW };

// Struct for Patient
struct Patient {
    int id;
    string name;
    Priority priority;
    Patient(int id, string name, Priority priority) : id(id), name(name), priority(priority) {}
};

// Compare function for priority queue
struct ComparePatient {
    bool operator()(shared_ptr<Patient> a, shared_ptr<Patient> b) {
        if (a->priority == b->priority) {
            return a->id > b->id; // First-Come, First-Served for same priority
        }
        return a->priority > b->priority; // Higher priority comes first
    }
};

// Semaphore Implementation
class Semaphore {
private:
    int count;
    mutex mtx;
    condition_variable cv;

public:
    Semaphore(int initialCount) : count(initialCount) {}

    void acquire() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this] { return count > 0; });
        --count;
    }

    void release() {
        {
            lock_guard<mutex> lock(mtx);
            ++count;
        }
        cv.notify_one();
    }

    bool try_acquire() {
        lock_guard<mutex> lock(mtx);
        if (count > 0) {
            --count;
            return true;
        }
        return false;
    }

    int available() {
        lock_guard<mutex> lock(mtx);
        return count;
    }
};

// Shared resources
priority_queue<shared_ptr<Patient>, vector<shared_ptr<Patient>>, ComparePatient> patientQueue;
mutex queueMutex;
condition_variable cv;

// Semaphores for resource management
Semaphore doctorsAvailable(3);
Semaphore nursesAvailable(2);
Semaphore examRoomsAvailable(2);
Semaphore ventilatorsAvailable(1);

atomic<bool> isRunning(true);

// Helper function to convert priority to string
string priorityToString(Priority priority) {
    switch (priority) {
        case HIGH: return "High";
        case MEDIUM: return "Medium";
        case LOW: return "Low";
        default: return "Unknown";
    }
}

// Function to display the current state of resources
void displayState(const string& entity, int id, const string& name, const string& priority, const string& status) {
    cout << setw(10) << entity << setw(10) << id
         << setw(20) << name
         << setw(15) << priority
         << setw(20) << status
         << setw(10) << doctorsAvailable.available()
         << setw(10) << nursesAvailable.available()
         << setw(10) << examRoomsAvailable.available()
         << setw(10) << ventilatorsAvailable.available() << endl;
}

// Function for treating a patient
void treatPatient(int doctorId) {
    while (isRunning) {
        shared_ptr<Patient> currentPatient = nullptr;
        {
            unique_lock<mutex> lock(queueMutex);
            cv.wait(lock, [] { return !patientQueue.empty() || !isRunning; });

            if (!isRunning && patientQueue.empty()) break;

            currentPatient = patientQueue.top();
            patientQueue.pop();
        }

        doctorsAvailable.acquire(); // Acquire a doctor
        nursesAvailable.acquire();  // Acquire a nurse
        examRoomsAvailable.acquire(); // Acquire an exam room

        // Try to allocate ventilator if needed
        bool ventilatorAllocated = false;
        if (currentPatient->priority == HIGH) {
            if (ventilatorsAvailable.try_acquire()) {
                ventilatorAllocated = true;
            } else {
                cout << "Ventilator unavailable for " << currentPatient->name << endl;
            }
        }

        // Display treatment activity
        displayState("Doctor", doctorId, currentPatient->name, priorityToString(currentPatient->priority), "Treating...");

        this_thread::sleep_for(chrono::seconds(2)); // Simulating treatment time

        // Release resources
        if (ventilatorAllocated) {
            ventilatorsAvailable.release();
        }
        doctorsAvailable.release();  // Release the doctor
        nursesAvailable.release();   // Release the nurse
        examRoomsAvailable.release(); // Release the exam room

        // Display completion activity
        displayState("Doctor", doctorId, currentPatient->name, priorityToString(currentPatient->priority), "Finished");
    }
}

// Function for adding patients to the queue
void addPatient(int id, string name, Priority priority) {
    {
        lock_guard<mutex> lock(queueMutex);
        auto newPatient = make_shared<Patient>(id, name, priority);
        patientQueue.push(newPatient);

        // Display patient arrival
        displayState("Patient", id, name, priorityToString(priority), "Arrived");
    }
    cv.notify_one();
}

// Function to simulate patient arrivals
void patientArrival() {
    int patientId = 1;
    while (isRunning) {
        this_thread::sleep_for(chrono::seconds(rand() % 5 + 1)); // Random patient arrival time
        addPatient(patientId++, "Patient_" + to_string(patientId), Priority(rand() % 3));
    }
}

// Function to simulate dynamic resource generation (shift changes or emergencies)
void dynamicResourceGeneration() {
    while (isRunning) {
        this_thread::sleep_for(chrono::seconds(10)); // Simulate resource generation every 10 seconds
        {
            lock_guard<mutex> lock(queueMutex);
            int newDoctors = rand() % 2; // Randomly add 0 or 1 doctor
            int newNurses = rand() % 2;  // Randomly add 0 or 1 nurse
            int newExamRooms = rand() % 2; // Randomly add 0 or 1 exam room
            for (int i = 0; i < newDoctors; ++i) doctorsAvailable.release();
            for (int i = 0; i < newNurses; ++i) nursesAvailable.release();
            for (int i = 0; i < newExamRooms; ++i) examRoomsAvailable.release();

            if (newDoctors > 0 || newNurses > 0 || newExamRooms > 0) {
                cout << "Additional Resources: " << newDoctors << " doctor(s), " 
                     << newNurses << " nurse(s), and " << newExamRooms 
                     << " exam room(s) added due to shift changes or emergencies." << endl;
            }
        }
    }
}

// Function to simulate staff behavior, including fatigue and breaks
void staffBehavior() {
    while (isRunning) {
        this_thread::sleep_for(chrono::seconds(20)); // Simulate break time for staff every 20 seconds
        {
            lock_guard<mutex> lock(queueMutex);
            if (doctorsAvailable.try_acquire()) {
                // Simulate a doctor taking a break and temporarily reducing availability
                this_thread::sleep_for(chrono::seconds(5)); // Break duration
                doctorsAvailable.release();
                cout << "A doctor has returned from a break, increasing availability." << endl;
            }
        }
    }
}

// Main function
int main() {
    srand(time(0));

    cout << "Hospital Emergency Room Simulation Started..." << endl;

    // Display table headers
    cout << setw(10) << "Entity" << setw(10) << "ID"
         << setw(20) << "Name"
         << setw(15) << "Priority"
         << setw(20) << "Status"
         << setw(10) << "Doctors"
         << setw(10) << "Nurses"
         << setw(10) << "Rooms"
         << setw(10) << "Ventilators" << endl;

    cout << string(120, '-') << endl;

    // Create threads for doctors
    vector<thread> doctorThreads;
    for (int i = 0; i < 3; ++i) {
        doctorThreads.emplace_back(treatPatient, i + 1);
    }

    // Start patient arrival simulation
    thread patientThread(patientArrival);

    // Start dynamic resource generation
    thread resourceThread(dynamicResourceGeneration);

    // Start staff behavior simulation (breaks, fatigue)
    thread staffBehaviorThread(staffBehavior);

    // Let the simulation run for 30 seconds
    this_thread::sleep_for(chrono::seconds(30));
    isRunning = false;
    cv.notify_all(); // Wake up all waiting threads

    // Join threads
    for (auto &t : doctorThreads) {
        t.join();
    }
    patientThread.join();
    resourceThread.join();
    staffBehaviorThread.join();

    cout << "Hospital Emergency Room Simulation Ended." << endl;
    return 0;
}
