#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <condition_variable>
#include <string>
#include <algorithm>
#include <climits>

using namespace std;
using namespace chrono;

// 1. Таймер для измерений
class StopWatch {
private:
    time_point<high_resolution_clock> start_time;
public:
    void Start() {
        start_time = high_resolution_clock::now();
    }
    
    long long Stop() {
        auto end_time = high_resolution_clock::now();
        return duration_cast<nanoseconds>(end_time - start_time).count();
    }
};

// 2. Самодельный барьер (аналог C++20 barrier)
class SimpleBarrier {
private:
    mutex mtx;
    condition_variable cv;
    int count;
    int waiting;
    int generation;
    
public:
    explicit SimpleBarrier(int count) : count(count), waiting(0), generation(0) {}
    
    void wait() {
        unique_lock<mutex> lock(mtx);
        int gen = generation;
        
        if (++waiting == count) {
            generation++;
            waiting = 0;
            cv.notify_all();
        } else {
            cv.wait(lock, [this, gen] { return gen != generation; });
        }
    }
};

// 3. Самодельный семафор
class SimpleSemaphore {
private:
    mutex mtx;
    condition_variable cv;
    int count;
    
public:
    explicit SimpleSemaphore(int count = 1) : count(count) {}
    
    void acquire() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this] { return count > 0; });
        count--;
    }
    
    void release() {
        lock_guard<mutex> lock(mtx);
        count++;
        cv.notify_one();
    }
};

// 4. Базовый класс для всех тестов синхронизации
class SyncTest {
protected:
    int thread_count;
    int operations_per_thread;
    vector<thread> threads;
    atomic<int> shared_counter{0};
    string result_string;
    
public:
    SyncTest(int threads, int ops) : thread_count(threads), operations_per_thread(ops) {}
    virtual ~SyncTest() {}
    
    virtual void RunTest() = 0;
    virtual string GetName() const = 0;
    
    long long Measure() {
        StopWatch sw;
        sw.Start();
        RunTest();
        return sw.Stop();
    }
};

// 5. Тест с мьютексом
class MutexTest : public SyncTest {
private:
    mutex mtx;
    
public:
    MutexTest(int threads, int ops) : SyncTest(threads, ops) {}
    
    string GetName() const override { return "Мьютекс"; }
    
    void RunTest() override {
        threads.clear();
        shared_counter = 0;
        result_string.clear();
        
        auto worker = [this](int id) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(33, 126); // Печатные ASCII символы
            
            for (int i = 0; i < operations_per_thread; i++) {
                {
                    lock_guard<mutex> lock(mtx);
                    shared_counter++;
                    char c = static_cast<char>(dis(gen));
                    result_string += c;
                }
                this_thread::sleep_for(nanoseconds(10));
            }
        };
        
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
};

// 6. Тест с семафором
class SemaphoreTest : public SyncTest {
private:
    SimpleSemaphore sem;
    
public:
    SemaphoreTest(int threads, int ops) : SyncTest(threads, ops), sem(1) {}
    
    string GetName() const override { return "Семафор"; }
    
    void RunTest() override {
        threads.clear();
        shared_counter = 0;
        result_string.clear();
        
        auto worker = [this](int id) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(33, 126);
            
            for (int i = 0; i < operations_per_thread; i++) {
                sem.acquire();
                shared_counter++;
                char c = static_cast<char>(dis(gen));
                result_string += c;
                sem.release();
                this_thread::sleep_for(nanoseconds(10));
            }
        };
        
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
};

// 7. Тест с барьером
class BarrierTest : public SyncTest {
private:
    SimpleBarrier* bar;
    mutex mtx;
    
public:
    BarrierTest(int threads, int ops) : SyncTest(threads, ops) {
        bar = new SimpleBarrier(threads);
    }
    
    ~BarrierTest() {
        delete bar;
    }
    
    string GetName() const override { return "Барьер"; }
    
    void RunTest() override {
        threads.clear();
        shared_counter = 0;
        result_string.clear();
        
        auto worker = [this](int id) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(33, 126);
            
            for (int i = 0; i < operations_per_thread; i++) {
                char c = static_cast<char>(dis(gen));
                
                bar->wait(); // Все потоки ждут здесь
                
                {
                    lock_guard<mutex> lock(mtx);
                    if (id == 0) {
                        shared_counter++;
                        result_string += c;
                    }
                }
                
                this_thread::sleep_for(nanoseconds(10));
            }
        };
        
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
};

// 8. Тест со спинлоком
class SpinLockTest : public SyncTest {
private:
    atomic_flag lock = ATOMIC_FLAG_INIT;
    
    void Acquire() {
        while (lock.test_and_set(memory_order_acquire)) {
            // Активное ожидание
        }
    }
    
    void Release() {
        lock.clear(memory_order_release);
    }
    
public:
    SpinLockTest(int threads, int ops) : SyncTest(threads, ops) {}
    
    string GetName() const override { return "Спинлок"; }
    
    void RunTest() override {
        threads.clear();
        shared_counter = 0;
        result_string.clear();
        
        auto worker = [this](int id) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(33, 126);
            
            for (int i = 0; i < operations_per_thread; i++) {
                Acquire();
                shared_counter++;
                char c = static_cast<char>(dis(gen));
                result_string += c;
                Release();
                this_thread::sleep_for(nanoseconds(10));
            }
        };
        
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
};

// 9. Тест со SpinWait
class SpinWaitTest : public SyncTest {
private:
    atomic<bool> locked{false};
    
    void Acquire() {
        while (true) {
            // Сначала активное ожидание
            for (int i = 0; i < 1000; i++) {
                bool expected = false;
                if (locked.compare_exchange_weak(expected, true, memory_order_acquire)) {
                    return;
                }
            }
            // Потом уступка процессора
            this_thread::yield();
        }
    }
    
    void Release() {
        locked.store(false, memory_order_release);
    }
    
public:
    SpinWaitTest(int threads, int ops) : SyncTest(threads, ops) {}
    
    string GetName() const override { return "SpinWait"; }
    
    void RunTest() override {
        threads.clear();
        shared_counter = 0;
        result_string.clear();
        
        auto worker = [this](int id) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(33, 126);
            
            for (int i = 0; i < operations_per_thread; i++) {
                Acquire();
                shared_counter++;
                char c = static_cast<char>(dis(gen));
                result_string += c;
                Release();
                this_thread::sleep_for(nanoseconds(10));
            }
        };
        
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
};

// 10. Тест с монитором
class MonitorTest : public SyncTest {
private:
    mutex mtx;
    condition_variable cv;
    bool available = true;
    
public:
    MonitorTest(int threads, int ops) : SyncTest(threads, ops) {}
    
    string GetName() const override { return "Монитор"; }
    
    void Enter() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this] { return available; });
        available = false;
    }
    
    void Exit() {
        {
            lock_guard<mutex> lock(mtx);
            available = true;
        }
        cv.notify_one();
    }
    
    void RunTest() override {
        threads.clear();
        shared_counter = 0;
        result_string.clear();
        
        auto worker = [this](int id) {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(33, 126);
            
            for (int i = 0; i < operations_per_thread; i++) {
                Enter();
                shared_counter++;
                char c = static_cast<char>(dis(gen));
                result_string += c;
                Exit();
                this_thread::sleep_for(nanoseconds(10));
            }
        };
        
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
};

// 11. Класс для запуска и сравнения тестов
class BenchmarkRunner {
private:
    vector<unique_ptr<SyncTest>> tests;
    int thread_count;
    int operations_per_thread;
    int iterations;
    
public:
    BenchmarkRunner(int threads, int ops, int iter) 
        : thread_count(threads), operations_per_thread(ops), iterations(iter) {
        
        tests.push_back(make_unique<MutexTest>(threads, ops));
        tests.push_back(make_unique<SemaphoreTest>(threads, ops));
        tests.push_back(make_unique<BarrierTest>(threads, ops));
        tests.push_back(make_unique<SpinLockTest>(threads, ops));
        tests.push_back(make_unique<SpinWaitTest>(threads, ops));
        tests.push_back(make_unique<MonitorTest>(threads, ops));
    }
    
    void Run() {
        cout << "--------------------------------------------------\n";
        cout << "РЕЗУЛЬТАТЫ БЕНЧМАРКА ПРИМИТИВОВ СИНХРОНИЗАЦИИ\n";
        cout << "--------------------------------------------------\n";
        cout << "Количество потоков: " << thread_count << "\n";
        cout << "Операций на поток: " << operations_per_thread << "\n";
        cout << "Итераций теста: " << iterations << "\n";
        cout << "--------------------------------------------------\n\n";
        
        vector<pair<string, vector<long long>>> results;
        
        for (auto& test : tests) {
            cout << "Запуск теста: " << test->GetName() << "...\n";
            vector<long long> times;
            
            for (int i = 0; i < iterations; i++) {
                long long time_ns = test->Measure();
                times.push_back(time_ns);
                cout << "  Итерация " << (i+1) << ": " << time_ns << " наносекунд\n";
            }
            
            long long sum = 0;
            for (auto t : times) sum += t;
            long long avg = sum / iterations;
            
            results.emplace_back(test->GetName(), times);
            
            cout << "  Среднее время: " << avg << " наносекунд\n";
            cout << "  Пропускная способность: " 
                 << (thread_count * operations_per_thread * 1e9) / avg 
                 << " операций/сек\n\n";
        }
        
        PrintComparisonTable(results);
    }
    
private:
    void PrintComparisonTable(const vector<pair<string, vector<long long>>>& results) {
        cout << "--------------------------------------------------\n";
        cout << "СРАВНИТЕЛЬНЫЙ АНАЛИЗ ПРОИЗВОДИТЕЛЬНОСТИ\n";
        cout << "--------------------------------------------------\n";
        cout << "Примитив\t\tСр. время (нс)\tКоэф. скорости\n";
        cout << "---------\t\t-------------\t-------------\n";
        
        long long best_time = LONG_LONG_MAX;
        for (const auto& result : results) {
            long long sum = 0;
            for (auto t : result.second) sum += t;
            long long avg = sum / iterations;
            if (avg < best_time) best_time = avg;
        }
        
        for (const auto& result : results) {
            long long sum = 0;
            for (auto t : result.second) sum += t;
            long long avg = sum / iterations;
            double speed_factor = static_cast<double>(avg) / best_time;
            
            printf("%-15s\t%12lld нс\t%12.2fx\n", 
                   result.first.c_str(), avg, speed_factor);
        }
        
        cout << "\nПримечание: Меньшее время и коэффициент скорости - лучше.\n";
        cout << "--------------------------------------------------\n\n";
        
        cout << "РЕКОМЕНДАЦИИ ПО ВЫБОРУ ПРИМИТИВА:\n";
        cout << "---------------------------\n";
        cout << "1. СПИНЛОК - лучший для ОЧЕНЬ коротких критических секций (< 100 нс)\n";
        cout << "2. МЬЮТЕКС - лучший универсальный выбор для большинства задач\n";
        cout << "3. SPINWAIT - хорош для смешанных нагрузок\n";
        cout << "4. МОНИТОР - лучший для сложных паттернов синхронизации\n";
        cout << "5. СЕМАФОР - для ограничения доступа к пулу ресурсов\n";
        cout << "6. БАРЬЕР - для синхронизации фаз параллельных алгоритмов\n";
    }
};

// 12. Демонстрация ASCII гонки
void DemonstrateASCIIRace() {
    cout << "\n\nДЕМОНСТРАЦИЯ ASCII ГОНКИ\n";
    cout << "-------------------------\n";
    cout << "Несколько потоков генерируют случайные символы одновременно.\n";
    cout << "Без синхронизации символы перемешивались бы беспорядочно.\n";
    cout << "С синхронизацией каждый поток ждет своей очереди.\n\n";
    
    const int thread_count = 4;
    const int chars_per_thread = 8;
    
    mutex cout_mutex;
    vector<thread> threads;
    
    cout << "Начало гонки! 4 потока генерируют символы:\n";
    
    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back([i, &cout_mutex]() {
            random_device rd;
            mt19937 gen(rd());
            uniform_int_distribution<> dis(33, 126); // !"#$%&'()*+,-./0-9:;<=>?@A-Z[\]^_`a-z{|}~
            
            string thread_chars;
            for (int j = 0; j < chars_per_thread; j++) {
                thread_chars += static_cast<char>(dis(gen));
                this_thread::sleep_for(milliseconds(100));
                
                {
                    lock_guard<mutex> lock(cout_mutex);
                    cout << "Поток " << i << ": ";
                    for (int k = 0; k <= j; k++) {
                        cout << thread_chars[k];
                    }
                    cout << endl;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    cout << "\nГонка завершена! Все потоки закончили генерацию.\n";
}

// 14. Главная функция
int main() {
    cout << "ГОНКА ASCII СИМВОЛОВ - СРАВНЕНИЕ ПРИМИТИВОВ СИНХРОНИЗАЦИИ\n";
    cout << "--------------------------------------------------======\n\n";
    
    int thread_count = 4;           // Количество потоков
    int operations_per_thread = 500; // Операций на поток
    int benchmark_iterations = 3;    // Итераций для точности
    
    cout << "НАСТРОЙКИ ТЕСТА:\n";
    cout << "- Потоков: " << thread_count << "\n";
    cout << "- Операций на поток: " << operations_per_thread << "\n";
    cout << "- Итераций: " << benchmark_iterations << "\n\n";
    
    cout << "Каждая операция:\n";
    cout << "1. Захват примитива синхронизации\n";
    cout << "2. Увеличение общего счетчика\n";
    cout << "3. Генерация случайного ASCII символа\n";
    cout << "4. Добавление символа в общую строку\n";
    cout << "5. Освобождение примитива\n";
    cout << "6. Короткая пауза (10 нс)\n\n";
    
    // Запускаем бенчмарк
    BenchmarkRunner benchmark(thread_count, operations_per_thread, benchmark_iterations);
    benchmark.Run();
    
    // Демонстрация гонки
    DemonstrateASCIIRace();
    
    cout << "\n\n--------------------------------------------------\n";
    cout << "ВЫВОДЫ И РЕЗУЛЬТАТЫ:\n";
    cout << "--------------------------------------------------\n";
    cout << "1. Для коротких операций (< 100 нс) спинлок быстрее\n";
    cout << "2. Для длинных операций мьютекс эффективнее\n";
    cout << "3. SpinWait - золотая середина для смешанных задач\n";
    cout << "4. Выбор примитива зависит от конкретной задачи\n";
    cout << "5. Лучше тестирвать на реальной нагрузке\n";
    
    return 0;
}
