#include <iostream>
#include <pthread.h>
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <queue>
#include <dirent.h>
#include <algorithm>
#include <set>


struct Mapper {
	std::vector<std::string> files_to_process;
	std::vector<int> file_ids;
	std::vector<std::pair<std::string, int>>* words;
	pthread_mutex_t* mtx;
	pthread_barrier_t* help_sync;
};

struct Reducer {
	std::vector<char> letters;
	std::vector<std::pair<std::string, int>>* words;
	pthread_mutex_t* mtx;
	pthread_barrier_t* help_sync;
};

struct MapReduce {
	bool is_mapper;
	Mapper* mapper;
	Reducer* reducer;
};

// functie care compara doua perechi {cuvant, lista fisiere} dupa dimensiunea listei, iar apoi alfabetic
bool comp (const std::pair<std::string, std::set<int, std::less<int>>>& a, const std::pair<std::string, std::set<int, std::less<int>>>& b) {
	if (a.second.size() == b.second.size()) {
		return a.first < b.first;
	}
	return a.second.size() > b.second.size();
}


// functie care sorteaza vectorul de cuvinte
void sort_pairs(std::vector<std::pair<std::string, std::set<int, std::less<int>>> >& words) {
	std::sort(words.begin(), words.end(), comp);
}


void* mapper (void* arg) {
	// facem cast de tip Mapper argumentului primit
	Mapper* mapper = static_cast<Mapper*>(arg);
	std::vector<std::string> files_to_process = mapper->files_to_process;
	std::vector<std::pair<std::string, int>>* words = mapper->words;
	std::vector<int> file_ids = mapper->file_ids;
	

	// parcurgem lista cu numele fisierelor
	for (int i = 0; i < files_to_process.size(); i++) {
		std::string file = files_to_process[i];
		int file_id = file_ids[i];
		// deschidem fisierul curent
		std::ifstream input_file(file);
		if (!input_file.is_open()) {
			std::cout << "Error opening file " << file << std::endl;
			return nullptr;
		}

		// blocam mutexul pentru a avea acces exclusiv la lista de cuvinte
		pthread_mutex_lock(mapper->mtx);

		std::string line;
		while (std::getline(input_file, line)) {
			char letter;
			std::string word = "";
			// parcurgem linia curenta
			for (int i = 0; i < line.size(); i++) {
				letter = line[i];
				// daca nu am ajuns la un caracter delimitator,
				// transformam litera in lowercase si o adaugam la cuvant
				if (letter != ' ' && letter != '\n') {
					if (isalpha(letter)) {
						letter = tolower(line[i]);
						word += letter;
					}
				} else {
					if (word.size() > 0) {
						// adaugam cuvantul in lista
						words->push_back({word, file_id});		
						word = "";
					}
				}
			}
			if (word.size() > 0) {
				words->push_back({word, file_id});
			}
		}
		// deblocam mutexul pentru a elibera accesul la lista de cuvinte
		pthread_mutex_unlock(mapper->mtx);

		input_file.close();
	}
	
	// thread-ul va astepta la bariera pentru sincronizare
	pthread_barrier_wait(mapper->help_sync);

	pthread_exit(nullptr);
}

void* reducer(void* arg) {
	// facem cast de tip Reducer argumentului primit
	Reducer* reducer = static_cast<Reducer*>(arg);

	pthread_mutex_t* mtx = reducer->mtx;
	std::vector<std::pair<std::string, int>>* words = reducer->words;
	std::vector<char> letters = reducer->letters;

	// thread-ul trebuie sa astepte la bariera pentru sincronizare
	pthread_barrier_wait(reducer->help_sync);

	// parcurgem cuvintele care incep cu literele asignate reducerului
	for (int i = 0; i < letters.size(); i++) {
		// extragem litera curenta din vector si cream map-ul sau specific
		char current_letter = letters[i];
		std::map<std::string, std::set<int, std::less<int> >> current_letter_map = {};

		// parcurgem vectorul de perechi de cuvinte
		for (int j = 0; j < (*words).size(); j++) {
			// extragem perechea curenta din vector
			std::pair<std::string, int> word_pair = words->at(j);
			std::string word = word_pair.first;
			int file_id = word_pair.second;
			// daca incepe cu litera la care suntem, adaugam cuvantul in map
			if (word[0] == current_letter) {
				if (current_letter_map.find(word) == current_letter_map.end()) {
					current_letter_map[word] = {file_id};
				} else {
					current_letter_map[word].insert(file_id);
				}

			}
			
		}

		// sortam map-ul dupa dimensiunea set-ului de id-uri, iar apoi alfabetic
		std::vector<std::pair<std::string, std::set<int, std::less<int>>> > sorted_map;
		for (auto pair : current_letter_map) {
			sorted_map.push_back(pair);
		}
		sort_pairs(sorted_map);
		
		std::string file_to_write = "";
		file_to_write += current_letter;
		file_to_write += ".txt";

		// deschidem fisierul in care dorim sa scriem cuvintele
		std::ofstream output(file_to_write);
		if (!output.is_open()) {
			std::cout << "Error opening file " << file_to_write << std::endl;
			return nullptr;
		}
		// parcurgem map-ul sortat si formatam rezultatul conform cerintei
		for (auto it = sorted_map.begin(); it != sorted_map.end(); it++) {
			output << it->first << ":[";
			for (auto it2 = it->second.begin(); it2 != it->second.end(); it2++) {
				output << *it2;
				auto it3 = it2;
				it3++;
				if (it3 != it->second.end()) {
					output << " ";
				}
			}
			output << "]" << std::endl;
		}
	}
	

	pthread_exit(nullptr);
}

void* compute_threads(void* arg) {
	// facem cast de tip MapReduce argumentului primit
	MapReduce* map_reduce = static_cast<MapReduce*>(arg);

	// daca a fost apelat un thread Mapper, vom apela functia mapper
	if (map_reduce->is_mapper) {
		mapper(map_reduce->mapper);
	} 

	// altfel apelam reducer
	if (!map_reduce->is_mapper) {
		reducer(map_reduce->reducer);
	}
	pthread_exit(nullptr);
}


int main(int argc, const char* argv[]) {

	// am definit mecanismele de sincronizare
	pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
	pthread_barrier_t help_sync;

	if (argc < 4) {
		return 1;
	}

	// extragem numarul de mapperi si reduceri din argumente
	int no_of_mappers = atoi(argv[1]);
	int no_of_reducers = atoi(argv[2]);

	// alte variabile care vor fi utile in continuare
	std::string line;
	int no_of_files;
	std::vector<std::string> files_to_open;
	std::vector<std::pair<std::string, int>> words;
	std::vector<MapReduce> all_threads(no_of_mappers + no_of_reducers);
	std::vector<Mapper> mappers_list(no_of_mappers);
	std::vector<Reducer> reducers_list(no_of_reducers);

	// initializam bariera
	pthread_barrier_init(&help_sync, nullptr, no_of_mappers + no_of_reducers);

	// extragem fisierul din argumente
	std::string add_before_files = "../checker/";
	std::string path = add_before_files + argv[3];

	// deschidem fisierul de input
	std::ifstream input_file(path);
	if (!input_file.is_open()) {
		std::cout << "Error opening file " << argv[3] << std::endl;
		return 1;
	}

	// pe prima linie a fisierului se afla numarul de fisiere pe care trebuie sa le procesam
	input_file >> no_of_files;	

	// retinem intr-un vector fisierele pe care trebuie sa le parcurgem
	for (int i = 0; i < no_of_files; i++) {
		std::string file;
		input_file >> file;
		std::string complete = add_before_files + file;
		files_to_open.push_back(complete);
	}
	// inchidem fisierul de input
	input_file.close();


	// impartim fisierele pe care trebuie sa le procesam la mappers
	for (int i = 0; i < no_of_files; i++) {
		mappers_list[i % no_of_mappers].files_to_process.push_back(files_to_open[i]);
		mappers_list[i % no_of_mappers].file_ids.push_back(i + 1);		
	}

	// adaugam map-ul global si mutex-ul in fiecare mapper
	for (int i = 0; i < no_of_mappers; i++) {
		mappers_list[i].mtx = &mtx;
		mappers_list[i].words = &words;
		mappers_list[i].help_sync = &help_sync;
	}

	// impartim literele alfabetului in mod cat mai echitabil la numarul de reduceri
	for (char letter = 'a'; letter <= 'z'; letter++) {
		int reducer_number = (letter - 'a') % no_of_reducers;
		reducers_list[reducer_number].letters.push_back(letter);
	}

	// adaugam map-ul global si mutex-ul in fiecare reducer
	for (int i = 0; i < no_of_reducers; i++) {
		reducers_list[i].mtx = &mtx;
		reducers_list[i].words = &words;
		reducers_list[i].help_sync = &help_sync;
	}

	// imbinam mappers si reducers
	for (int i = 0; i < no_of_mappers; i++) {
		all_threads[i].is_mapper = true;
		all_threads[i].mapper = &mappers_list[i];
		all_threads[i].reducer = nullptr;
	}

	for (int i = no_of_mappers; i < no_of_mappers + no_of_reducers; i++) {
		all_threads[i].is_mapper = false;
		all_threads[i].mapper = nullptr;
		all_threads[i].reducer = &reducers_list[i - no_of_mappers];

	}

	// cream thread-urile
	pthread_t threads[no_of_mappers + no_of_reducers];
	for (int i = 0; i < no_of_mappers + no_of_reducers; i++) {
		if (pthread_create(&threads[i], nullptr, compute_threads, &all_threads[i])) {
			std::cout << "Error creating thread " << i << std::endl;
			return 1;
		}
	}

	// asteptam ca toate thread-urile sa isi termine executia
	for (int i = 0; i < no_of_mappers + no_of_reducers; i++) {
		if (pthread_join(threads[i], nullptr)) {
			std::cout << "Error joining thread " << i << std::endl;
			return 1;
		}
	}
	
	// eliberam memoria pentru bariera
	pthread_barrier_destroy(&help_sync);

	return 0;
}