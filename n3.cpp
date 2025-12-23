#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <random>
#include <atomic>
#include <queue>
#include <sstream>

enum class Priority {
    READERS,     // Приоритет читателей
    WRITERS,     // Приоритет писателей
    FAIR         // Честное распределение
};

class ReadersWriters {
private:
    std::mutex mtx;
    std::condition_variable cv_read;
    std::condition_variable cv_write;
    
    int active_readers = 0;
    int waiting_readers = 0;
    int active_writers = 0;
    int waiting_writers = 0;
    
    Priority priority;
    bool writer_active = false;
    
    // Для честного режима
    std::queue<bool> request_queue; // true - writer, false - reader
    
public:
    ReadersWriters(Priority p = Priority::FAIR) : priority(p) {}
    
    void start_read() {
        std::unique_lock<std::mutex> lock(mtx);
        
        if (priority == Priority::FAIR) {
            // Для честного режима добавляем запрос в очередь
            request_queue.push(false);
            
            // Ждем своей очереди и отсутствия активных писателей
            cv_read.wait(lock, [this]() {
                return !writer_active && 
                       !request_queue.empty() && 
                       !request_queue.front();
            });
            
            request_queue.pop();
        } else {
            waiting_readers++;
            if (priority == Priority::READERS) {
                // Приоритет читателей: ждем только активных писателей
                cv_read.wait(lock, [this]() { return !writer_active; });
            } else { // Priority::WRITERS
                // Приоритет писателей: ждем отсутствия активных писателей и писателей в очереди
                cv_read.wait(lock, [this]() { 
                    return !writer_active && waiting_writers == 0; 
                });
            }
            waiting_readers--;
        }
        
        active_readers++;
    }
    
    void end_read() {
        std::unique_lock<std::mutex> lock(mtx);
        active_readers--;
        
        if (active_readers == 0) {
            // Если читателей не осталось, можно будить писателей
            cv_write.notify_one();
        }
        
        // В честном режиме будим следующего в очереди
        if (priority == Priority::FAIR && !request_queue.empty()) {
            if (request_queue.front()) { // writer
                cv_write.notify_one();
            } else { // reader
                cv_read.notify_all(); // может быть несколько читателей
            }
        }
    }
    
    void start_write() {
        std::unique_lock<std::mutex> lock(mtx);
        
        if (priority == Priority::FAIR) {
            // Для честного режима добавляем запрос в очередь
            request_queue.push(true);
            
            // Ждем своей очереди и отсутствия активных читателей/писателей
            cv_write.wait(lock, [this]() {
                return !writer_active && active_readers == 0 &&
                       !request_queue.empty() && 
                       request_queue.front();
            });
            
            request_queue.pop();
        } else {
            waiting_writers++;
            if (priority == Priority::READERS) {
                // Приоритет читателей: ждем отсутствия активных читателей и писателей
                cv_write.wait(lock, [this]() { 
                    return !writer_active && active_readers == 0; 
                });
            } else { // Priority::WRITERS
                // Приоритет писателей: ждем только активных читателей и писателей
                cv_write.wait(lock, [this]() { 
                    return !writer_active && active_readers == 0; 
                });
            }
            waiting_writers--;
        }
        
        writer_active = true;
        active_writers++;
    }
    
    void end_write() {
        std::unique_lock<std::mutex> lock(mtx);
        writer_active = false;
        active_writers--;
        
        // Будим ожидающих
        if (priority == Priority::READERS) {
            // Приоритет читателей: сначала читатели
            if (waiting_readers > 0) {
                cv_read.notify_all();
            } else if (waiting_writers > 0) {
                cv_write.notify_one();
            }
        } else if (priority == Priority::WRITERS) {
            // Приоритет писателей: сначала писатели
            if (waiting_writers > 0) {
                cv_write.notify_one();
            } else if (waiting_readers > 0) {
                cv_read.notify_all();
            }
        } else { // Priority::FAIR
            // Честный режим: будим следующего в очереди
            if (!request_queue.empty()) {
                if (request_queue.front()) { // writer
                    cv_write.notify_one();
                } else { // reader
                    cv_read.notify_all();
                }
            }
        }
    }
    
    void set_priority(Priority new_priority) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Очищаем очередь при смене приоритета
        if (priority == Priority::FAIR && new_priority != Priority::FAIR) {
            // Переход из честного режима
            while (!request_queue.empty()) {
                request_queue.pop();
            }
        } else if (new_priority == Priority::FAIR) {
            // Переход в честный режим
            while (!request_queue.empty()) {
                request_queue.pop();
            }
        }
        
        priority = new_priority;
        
        // Пробуждаем все потоки для переоценки условий
        cv_read.notify_all();
        cv_write.notify_all();
    }
    
    void print_status(const std::string& prefix = "") {
        std::unique_lock<std::mutex> lock(mtx);
        std::stringstream ss;
        if (!prefix.empty()) {
            ss << prefix << " ";
        }
        ss << "Активные читатели: " << active_readers 
           << ", Ожидающие читатели: " << waiting_readers
           << ", Активные писатели: " << active_writers
           << " (writer_active: " << (writer_active ? "да" : "нет") << ")"
           << ", Ожидающие писатели: " << waiting_writers;
        
        if (priority == Priority::FAIR) {
            ss << ", Очередь запросов: " << request_queue.size();
        }
        
        ss << ", Приоритет: ";
        switch(priority) {
            case Priority::READERS: ss << "ЧИТАТЕЛИ"; break;
            case Priority::WRITERS: ss << "ПИСАТЕЛИ"; break;
            case Priority::FAIR: ss << "ЧЕСТНЫЙ"; break;
        }
        
        std::cout << ss.str() << std::endl;
    }
};

// Глобальная разделяемая переменная
std::atomic<int> shared_data{0};
ReadersWriters rw;

// Функция для читателя
void reader(int id, int read_count) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(50, 200);
    
    for (int i = 0; i < read_count; ++i) {
        // Имитация работы перед чтением
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
        
        std::cout << "Читатель " << id << " хочет читать (итерация " << i+1 << ")" << std::endl;
        rw.start_read();
        
        // Чтение данных
        int value = shared_data.load();
        std::cout << "Читатель " << id << " читает: " << value 
                  << " (итерация " << i+1 << ")" << std::endl;
        
        // Имитация времени чтения
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)/2));
        
        rw.end_read();
        std::cout << "Читатель " << id << " закончил чтение" << std::endl;
    }
}

// Функция для писателя
void writer(int id, int write_count) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100, 400);
    
    for (int i = 0; i < write_count; ++i) {
        // Имитация работы перед записью
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
        
        std::cout << "Писатель " << id << " хочет писать (итерация " << i+1 << ")" << std::endl;
        rw.start_write();
        
        // Запись данных
        int new_value = id * 100 + i;
        shared_data.store(new_value);
        std::cout << "Писатель " << id << " пишет: " << new_value 
                  << " (итерация " << i+1 << ")" << std::endl;
        
        // Имитация времени записи
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)/2));
        
        rw.end_write();
        std::cout << "Писатель " << id << " закончил запись" << std::endl;
    }
}

int main() {
    std::cout << "=== Решение задачи 'Читатели-Писатели' с выбором приоритета ===\n" << std::endl;
    
    // Настройка параметров
    const int NUM_READERS = 3;
    const int NUM_WRITERS = 2;
    const int READS_PER_READER = 3;
    const int WRITES_PER_WRITER = 2;
    
    // Тестирование разных приоритетов
    std::vector<Priority> priorities = {
        Priority::READERS,
        Priority::WRITERS,
        Priority::FAIR
    };
    
    for (auto priority : priorities) {
        std::cout << "\n\n=== Тестирование с приоритетом: ";
        switch(priority) {
            case Priority::READERS: 
                std::cout << "ЧИТАТЕЛИ (читатели имеют приоритет)"; 
                break;
            case Priority::WRITERS: 
                std::cout << "ПИСАТЕЛИ (писатели имеют приоритет)"; 
                break;
            case Priority::FAIR: 
                std::cout << "ЧЕСТНЫЙ (FIFO)"; 
                break;
        }
        std::cout << " ===" << std::endl;
        
        // Сбрасываем разделяемую переменную
        shared_data.store(0);
        
        // Устанавливаем приоритет
        rw.set_priority(priority);
        
        // Создаем потоки
        std::vector<std::thread> threads;
        
        // Писатели
        for (int i = 0; i < NUM_WRITERS; ++i) {
            threads.emplace_back(writer, i + 1, WRITES_PER_WRITER);
        }
        
        // Читатели
        for (int i = 0; i < NUM_READERS; ++i) {
            threads.emplace_back(reader, i + 1, READS_PER_READER);
        }
        
        // Периодически выводим статус
        std::thread status_thread([priority]() {
            for (int i = 0; i < 5; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                rw.print_status("[СТАТУС]");
            }
        });
        
        // Ждем завершения всех потоков
        for (auto& t : threads) {
            t.join();
        }
        
        status_thread.join();
        
        std::cout << "\nФинальное значение shared_data: " << shared_data.load() << std::endl;
        std::cout << "=== Завершено ===" << std::endl;
        
        // Небольшая пауза между тестами
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Демонстрация динамического изменения приоритета
    std::cout << "\n\n=== Демонстрация динамического изменения приоритета ===" << std::endl;
    
    shared_data.store(0);
    rw.set_priority(Priority::FAIR);
    
    // Создаем потоки с большим количеством операций
    std::thread reader1([]() { reader(1, 15); });
    std::thread writer1([]() { writer(1, 10); });
    std::thread reader2([]() { reader(2, 15); });
    
    // Меняем приоритет во время выполнения
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    std::cout << "\n>>> Меняем приоритет на WRITERS <<<\n" << std::endl;
    rw.set_priority(Priority::WRITERS);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    std::cout << "\n>>> Меняем приоритет на READERS <<<\n" << std::endl;
    rw.set_priority(Priority::READERS);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    std::cout << "\n>>> Меняем приоритет на FAIR <<<\n" << std::endl;
    rw.set_priority(Priority::FAIR);
    
    reader1.join();
    writer1.join();
    reader2.join();
    
    return 0;
}