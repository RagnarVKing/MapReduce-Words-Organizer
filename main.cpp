#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>
#include <map>
#include <queue>
#include <set>

using namespace std;

struct File
{
    string name;
    int id;

    File(string &n, int i) : name(n), id(i) {}
};

struct Data
{
    queue<File> &files;
    int nr_files;
    int nr_map;
    int nr_red;
    int &nr_processed;
    pthread_mutex_t &mutex_map;
    pthread_barrier_t &barrier;
    pthread_barrier_t &red_barrier;
    pthread_barrier_t &map_barrier;
    pthread_mutex_t letters_mutex[26];
    queue<char> &q;
    vector<map<string, set<int>>> &lists;
    queue<map<string, set<int>>*> &maps;
    map<string, set<int>> &words_list;
    vector<vector<pair<string, set<int>>>> &alphabet;

    Data(queue<File> &f, int n_f, int n_m, int n_r, int &n_p, pthread_mutex_t &m_m, pthread_barrier_t &b, pthread_barrier_t &r_b, pthread_barrier_t &m_b, queue<char> &q, vector<map<string, set<int>>> &l, queue<map<string, set<int>>*> &m, map<string, set<int>> &wl, vector<vector<pair<string, set<int>>>> &a) : files(f), nr_files(n_f), nr_map(n_m), nr_red(n_r), nr_processed(n_p), mutex_map(m_m), barrier(b), red_barrier(r_b), map_barrier(m_b), q{q}, lists(l), maps(m), words_list(wl), alphabet(a) {
        for(int i = 0; i < 26; i++) {
            pthread_mutex_init(&letters_mutex[i], nullptr);
        }
    }

    ~Data() {
        for(int i = 0; i < 26; i++) {
            pthread_mutex_destroy(&letters_mutex[i]);
        }
    }
};

struct Args
{
    Data *data;
    int id = -1;

    Args(Data *d, int i) : data(d), id(i) {}
};

bool cmp(pair<string, set<int>> &a, pair<string, set<int>> &b)
{
    if (a.second.size() == b.second.size()) {
        return a.first < b.first;
    }
    return a.second.size() > b.second.size();
}

void *f(void *arg)
{
    Args *args = (Args *)arg;

    if (args->id <= args->data->nr_map) {

        while (true) {

            pthread_mutex_lock(&args->data->mutex_map);

            if (args->data->files.empty()) {
                pthread_mutex_unlock(&args->data->mutex_map);
                break;
            }

            File file = args->data->files.front();
            args->data->files.pop();

            pthread_mutex_unlock(&args->data->mutex_map);

            ifstream fin(file.name);
            if (!fin.is_open()) {
                pthread_mutex_lock(&args->data->mutex_map);
                cerr << "ERROR: file not opened\n";
                pthread_mutex_unlock(&args->data->mutex_map);
                return (void *)1;
            }

            string word;
            while (fin >> word) {
                transform(word.begin(), word.end(), word.begin(), ::tolower);
                word.erase(remove_if(word.begin(), word.end(), [](char c) {
                    return c < 'a' || c > 'z';
                }), word.end());

                if (!word.empty()) {
                    args->data->lists[file.id - 1][word].insert(file.id);
                }
            }
            fin.close();
        }

        pthread_barrier_wait(&args->data->map_barrier);

        pthread_mutex_lock(&args->data->mutex_map);
        if (args->data->nr_processed == 0) {
            for (int i = 0; i < args->data->nr_files; i++) {
                args->data->maps.push(&args->data->lists[i]);
            }
            args->data->nr_processed++;
        }
        pthread_mutex_unlock(&args->data->mutex_map);

        pthread_barrier_wait(&args->data->barrier);
    } else {
        pthread_barrier_wait(&args->data->barrier);

        pthread_mutex_lock(&args->data->mutex_map);
        args->data->nr_processed = 0;
        pthread_mutex_unlock(&args->data->mutex_map);

        while (true) {
            
            pthread_mutex_lock(&args->data->mutex_map);
            if (args->data->maps.empty()) {
                pthread_mutex_unlock(&args->data->mutex_map);
                break;
            }

            map<string, set<int>> *list = args->data->maps.front();
            args->data->maps.pop();

            pthread_mutex_unlock(&args->data->mutex_map);

            for (auto &it : *list) {
                pthread_mutex_lock(&args->data->letters_mutex[it.first[0] - 'a']);
                args->data->words_list[it.first].insert(it.second.begin(), it.second.end());
                pthread_mutex_unlock(&args->data->letters_mutex[it.first[0] - 'a']);
            }

        }

        pthread_barrier_wait(&args->data->red_barrier);

        while (true) {
            pthread_mutex_lock(&args->data->mutex_map);

            if (args->data->q.empty()) {
                pthread_mutex_unlock(&args->data->mutex_map);
                break;
            }

            char c = args->data->q.front();
            args->data->q.pop();

            pthread_mutex_unlock(&args->data->mutex_map);

            int ok = 0;
            for (auto &it : args->data->words_list) {
                if (it.first[0] == c) {
                    ok = 1;
                    args->data->alphabet[c - 'a'].push_back(it);
                } else if (ok == 1) {
                    break;
                }
            }
        }

        pthread_barrier_wait(&args->data->red_barrier);

        pthread_mutex_lock(&args->data->mutex_map);
        args->data->nr_processed = 0;
        pthread_mutex_unlock(&args->data->mutex_map);


        pthread_mutex_lock(&args->data->mutex_map);
        if (args->data->nr_processed == 0) {
            args->data->nr_processed++;
            for (char c = 'a'; c <= 'z'; c++) {
                args->data->q.push(c);
            }
        }
        pthread_mutex_unlock(&args->data->mutex_map);

        while (true) {
            pthread_mutex_lock(&args->data->mutex_map);
            if (args->data->q.empty()) {
                pthread_mutex_unlock(&args->data->mutex_map);
                break;
            }

            char c = args->data->q.front();
            args->data->q.pop();

            pthread_mutex_unlock(&args->data->mutex_map);

            sort(args->data->alphabet[c - 'a'].begin(), args->data->alphabet[c - 'a'].end(), cmp);

            string filename = string(1, c) + ".txt";
            ofstream fout(filename);

            for (auto &it : args->data->alphabet[c - 'a']) {
                fout << it.first << ":[";
                auto last = --it.second.end();

                for (auto iter = it.second.begin(); iter != it.second.end(); ++iter) {
                    fout << *iter;
                    if (iter != last) {
                        fout << " ";
                    }
                }

                fout << "]\n";
            }
            fout.close();
            
        }
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        cerr << "ERROR: different number of arguments!\n";
        return 1;
    }

    int nr_map = atoi(argv[1]);
    int nr_red = atoi(argv[2]);

    ifstream fin(argv[3]);
    if (!fin.is_open()) {
        cerr << "ERROR: file not opened\n";
        return 1;
    }

    int nr_files;
    queue<File> files;
    string fisier;
    string filename;

    fin >> nr_files;

    for (int i = 0; i < nr_files; i++) {
        fin >> fisier;
        filename = "../checker/" + fisier;
        files.push(File(filename, (i + 1)));
    }

    fin.close();

    pthread_t threads[nr_map + nr_red];
    int r;
    long id;
    void *status;
    Args *arguments[nr_map + nr_red];

    int nr_processed = 0;
    pthread_mutex_t mutex_map;
    pthread_mutex_init(&mutex_map, nullptr);
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, nullptr, nr_map + nr_red);
    pthread_barrier_t red_barrier;
    pthread_barrier_init(&red_barrier, nullptr, nr_red);
    pthread_barrier_t map_barrier;
    pthread_barrier_init(&map_barrier, nullptr, nr_map);

    queue<char> q;
    for (char c = 'a'; c <= 'z'; c++) {
        q.push(c);
    }

    vector<map<string, set<int>>> lists(nr_files);
    queue<map<string, set<int>>*> maps;
    map<string, set<int>> words_list;
    vector<vector<pair<string, set<int>>>> alphabet(26);

    Data data(files, nr_files, nr_map, nr_red, nr_processed, mutex_map, barrier, red_barrier, map_barrier, q, lists, maps, words_list, alphabet);

    for (id = 0; id < nr_map + nr_red; id++) {
        Args *args = new Args(&data, id + 1);
        arguments[id] = args;
        r = pthread_create(&threads[id], nullptr, f, (void *)arguments[id]);

        if (r) {
            cout << "Eroare la crearea thread-ului " << id + 1 << "\n";
            exit(-1);
        }
    }

    for (id = 0; id < nr_map + nr_red; id++) {
        r = pthread_join(threads[id], &status);

        if (r) {
            cout << "Eroare la asteptarea thread-ului " << id + 1 << "\n";
            exit(-1);
        }

        delete arguments[id];
    }

    pthread_mutex_destroy(&mutex_map);

    pthread_barrier_destroy(&barrier);
    pthread_barrier_destroy(&red_barrier);
    pthread_barrier_destroy(&map_barrier);

    return 0;
}