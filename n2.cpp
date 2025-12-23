#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <random>
#include <iomanip>

// Структура для хранения информации о фильме
struct Film {
    std::string title;
    int year;
    std::string genre;
    std::vector<std::string> directors;
    
    Film(const std::string& t, int y, const std::string& g, const std::vector<std::string>& d)
        : title(t), year(y), genre(g), directors(d) {}
};

// Глобальные переменные для многопоточности
std::mutex resultMutex;

// Функция для проверки, содержит ли фильм указанного режиссера
bool hasDirector(const Film& film, const std::string& targetDirector) {
    for (const auto& director : film.directors) {
        if (director == targetDirector) {
            return true;
        }
    }
    return false;
}

// Обработка данных без многопоточности
std::vector<Film> processWithoutThreads(const std::vector<Film>& films, const std::string& targetDirector) {
    std::vector<Film> result;
    
    for (const auto& film : films) {
        if (hasDirector(film, targetDirector)) {
            result.push_back(film);
        }
    }
    
    return result;
}

// Обработка части данных (для многопоточности)
void processChunk(const std::vector<Film>& films, 
                  std::vector<Film>& result, 
                  size_t start, 
                  size_t end, 
                  const std::string& targetDirector) {
    
    std::vector<Film> localResult;
    
    for (size_t i = start; i < end && i < films.size(); ++i) {
        if (hasDirector(films[i], targetDirector)) {
            localResult.push_back(films[i]);
        }
    }
    
    // Блокировка для добавления результатов в общий вектор
    std::lock_guard<std::mutex> lock(resultMutex);
    result.insert(result.end(), localResult.begin(), localResult.end());
}

// Обработка данных с использованием многопоточности
std::vector<Film> processWithThreads(const std::vector<Film>& films, 
                                     const std::string& targetDirector, 
                                     int numThreads) {
    std::vector<Film> result;
    std::vector<std::thread> threads;
    
    // Ограничиваем количество потоков, если данных меньше
    if (numThreads > static_cast<int>(films.size())) {
        numThreads = films.size();
    }
    
    size_t chunkSize = films.size() / numThreads;
    
    for (int i = 0; i < numThreads; ++i) {
        size_t start = i * chunkSize;
        size_t end = (i == numThreads - 1) ? films.size() : (i + 1) * chunkSize;
        
        threads.emplace_back(processChunk, 
                            std::ref(films), 
                            std::ref(result), 
                            start, 
                            end, 
                            std::ref(targetDirector));
    }
    
    // Ожидание завершения всех потоков
    for (auto& thread : threads) {
        thread.join();
    }
    
    return result;
}

// Функция для генерации тестовых данных
std::vector<Film> generateTestData(int dataSize) {
    std::vector<Film> films;
    films.reserve(dataSize);
    
    // Списки для генерации случайных данных
    std::vector<std::string> titles = {
        "Интерстеллар", "Начало", "Темный рыцарь", "Побег из Шоушенка",
        "Криминальное чтиво", "Форрест Гамп", "Зеленая миля", "Леон",
        "Бойцовский клуб", "Король Лев", "Матрица", "Список Шиндлера",
        "Властелин колец", "Гарри Поттер", "Пираты Карибского моря",
        "Титаник", "Аватар", "Звездные войны", "Паразиты", "Джокер"
    };
    
    std::vector<std::string> genres = {
        "Фантастика", "Драма", "Боевик", "Комедия", 
        "Триллер", "Детектив", "Мелодрама", "Приключения",
        "Фэнтези", "Ужасы", "Мюзикл", "Исторический"
    };
    
    // Режиссеры
    std::vector<std::string> directors = {
        "Кристофер Нолан",
        "Стивен Спилберг", 
        "Квентин Тарантино",
        "Джеймс Кэмерон",
        "Питер Джексон",
        "Ридли Скотт",
        "Роман Полански",
        "Роберт Земекис",
        "Дэвид Финчер",
        "Мартин Скорсезе",
        "Альфред Хичкок",
        "Фрэнсис Форд Коппола",
        "Стэнли Кубрик",
        "Тим Бёртон",
        "Гильермо дель Торо"
    };
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> yearDist(1980, 2023);
    std::uniform_int_distribution<> titleDist(0, titles.size() - 1);
    std::uniform_int_distribution<> genreDist(0, genres.size() - 1);
    std::uniform_int_distribution<> dirCountDist(1, 3);
    std::uniform_int_distribution<> dirDist(0, directors.size() - 1);
    
    for (int i = 0; i < dataSize; ++i) {
        std::string title = titles[titleDist(gen)] + " " + std::to_string(i + 1);
        int year = yearDist(gen);
        std::string genre = genres[genreDist(gen)];
        
        // Генерируем случайное количество режиссеров (1-3)
        int dirCount = dirCountDist(gen);
        std::vector<std::string> filmDirectors;
        
        for (int j = 0; j < dirCount; ++j) {
            filmDirectors.push_back(directors[dirDist(gen)]);
        }
        
        // Убираем дубликаты режиссеров
        std::sort(filmDirectors.begin(), filmDirectors.end());
        filmDirectors.erase(std::unique(filmDirectors.begin(), filmDirectors.end()), filmDirectors.end());
        
        films.emplace_back(title, year, genre, filmDirectors);
    }
    
    return films;
}

// Функция для вывода информации о фильме
void printFilm(const Film& film, int index = -1) {
    if (index != -1) {
        std::cout << "[" << index + 1 << "] ";
    }
    std::cout << film.title << " (" << film.year << "), " << film.genre << "\nРежиссеры: ";
    for (size_t i = 0; i < film.directors.size(); ++i) {
        std::cout << film.directors[i];
        if (i < film.directors.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << "\n" << std::endl;
}

// Функция для вывода списка фильмов
void printFilms(const std::vector<Film>& films, const std::string& message) {
    std::cout << "\n" << message << " (найдено " << films.size() << " фильмов):" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    if (films.empty()) {
        std::cout << "Фильмы не найдены" << std::endl;
    } else {
        for (size_t i = 0; i < films.size(); ++i) {
            printFilm(films[i], i);
        }
    }
    std::cout << std::string(60, '=') << "\n" << std::endl;
}

int main() {
    setlocale(LC_ALL, "Russian");
    
    int dataSize;           // Размер массива данных
    int numThreads;         // Количество потоков
    std::string targetDirector; // Режиссер для поиска
    
    std::cout << "МНОГОПОТОЧНАЯ ОБРАБОТКА ДАННЫХ О ФИЛЬМАХ" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "ВАЖНО: Для наглядности введите не менее 10000 фильмов" << std::endl;
    std::cout << "----------------------------------------\n" << std::endl;
    
    std::cout << "Введите размер массива данных (рекомендуется 10000-100000): ";
    std::cin >> dataSize;
    
    std::cout << "Введите количество потоков (1-16): ";
    std::cin >> numThreads;
    
    // Проверка ввода
    if (numThreads < 1) numThreads = 1;
    if (numThreads > 16) numThreads = 16;
    
    std::cin.ignore(); // Очистка буфера
    
    std::cout << "Введите имя режиссера для поиска (например: Кристофер Нолан): ";
    std::getline(std::cin, targetDirector);
    
    if (targetDirector.empty()) {
        targetDirector = "Кристофер Нолан";
    }
    
    // Генерация тестовых данных
    std::cout << "\nГенерация тестовых данных..." << std::endl;
    auto startGen = std::chrono::high_resolution_clock::now();
    std::vector<Film> films = generateTestData(dataSize);
    auto endGen = std::chrono::high_resolution_clock::now();
    auto durationGen = std::chrono::duration_cast<std::chrono::milliseconds>(endGen - startGen);
    
    std::cout << "Сгенерировано " << films.size() << " фильмов за " << durationGen.count() << " мс" << std::endl;
    
    // Статистика по режиссерам
    int countWithDirector = 0;
    for (const auto& film : films) {
        for (const auto& director : film.directors) {
            if (director == targetDirector) {
                countWithDirector++;
                break;
            }
        }
    }
    std::cout << "Примерно " << countWithDirector << " фильмов с режиссером " << targetDirector << "\n" << std::endl;
    
    // Обработка БЕЗ многопоточности
    std::cout << "1. ОБРАБОТКА БЕЗ МНОГОПОТОЧНОСТИ..." << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<Film> resultWithoutThreads = processWithoutThreads(films, targetDirector);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto durationWithoutThreads = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "   Завершено за " << durationWithoutThreads.count() << " мс" << std::endl;
    
    // Обработка С многопоточностью
    std::cout << "\n2. ОБРАБОТКА С МНОГОПОТОЧНОСТЬЮ (" << numThreads << " потоков)..." << std::endl;
    startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<Film> resultWithThreads = processWithThreads(films, targetDirector, numThreads);
    
    endTime = std::chrono::high_resolution_clock::now();
    auto durationWithThreads = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "   Завершено за " << durationWithThreads.count() << " мс" << std::endl;
    
    // Вывод результатов
    std::cout << "\n-----------------------------" << std::endl;
    std::cout << "РЕЗУЛЬТАТЫ ОБРАБОТКИ:" << std::endl;
    std::cout << "-------------------------------" << std::endl;
    std::cout << "Размер данных: " << dataSize << " фильмов" << std::endl;
    std::cout << "Режиссер для поиска: " << targetDirector << std::endl;
    std::cout << "Количество потоков: " << numThreads << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nВремя без многопоточности: " << durationWithoutThreads.count() << " мс" << std::endl;
    std::cout << "Время с многопоточностью:   " << durationWithThreads.count() << " мс" << std::endl;
    
    if (durationWithThreads.count() > 0) {
        double speedup = static_cast<double>(durationWithoutThreads.count()) / durationWithThreads.count();
        std::cout << "Ускорение: " << speedup << "x" << std::endl;
        
        if (speedup > 1.0) {
            std::cout << "✓ Многопоточная обработка быстрее на " 
                      << (durationWithoutThreads.count() - durationWithThreads.count()) 
                      << " мс (" << std::setprecision(1) << (speedup * 100 - 100) << "% быстрее)" << std::endl;
        } else if (speedup < 1.0) {
            std::cout << "✗ Многопоточная обработка МЕДЛЕННЕЕ из-за накладных расходов" << std::endl;
        } else {
            std::cout << "≈ Скорости примерно равны" << std::endl;
        }
    }
    
    // Проверка результатов
    std::cout << "\nПРОВЕРКА РЕЗУЛЬТАТОВ:" << std::endl;
    std::cout << "Фильмов найдено без потоков: " << resultWithoutThreads.size() << std::endl;
    std::cout << "Фильмов найдено с потоками:  " << resultWithThreads.size() << std::endl;
    
    if (resultWithoutThreads.size() == resultWithThreads.size()) {
        std::cout << "✓ Результаты идентичны по количеству" << std::endl;
    } else {
        std::cout << "✗ Результаты РАЗЛИЧАЮТСЯ!" << std::endl;
    }
    
    // Вывод найденных фильмов
    std::cout << "\n------------------------------" << std::endl;
    char showResults;
    std::cout << "Показать найденные фильмы? (y/n): ";
    std::cin >> showResults;
    
    if (showResults == 'y' || showResults == 'Y') {
        if (!resultWithThreads.empty()) {
            size_t maxToShow = std::min(resultWithThreads.size(), static_cast<size_t>(10));
            std::cout << "\nПЕРВЫЕ " << maxToShow << " НАЙДЕННЫХ ФИЛЬМОВ:" << std::endl;
            std::cout << std::string(60, '=') << std::endl;
            
            for (size_t i = 0; i < maxToShow; ++i) {
                printFilm(resultWithThreads[i], i);
            }
            
            if (resultWithThreads.size() > maxToShow) {
                std::cout << "... и еще " << (resultWithThreads.size() - maxToShow) << " фильмов" << std::endl;
            }
        } else {
            std::cout << "Фильмы не найдены" << std::endl;
        }
    }
    
    // Пример вывода нескольких фильмов из общего списка
    std::cout << "\n-------------------------" << std::endl;
    std::cout << "ПЕРВЫЕ 3 ФИЛЬМА ИЗ ОБЩЕГО СПИСКА:" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    for (int i = 0; i < std::min(3, static_cast<int>(films.size())); ++i) {
        printFilm(films[i], i);
    }

    
    return 0;
}