/*
 * Copyright 2016 Matthieu Gautier <mgautier@kymeria.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU  General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef OPENZIM_ZIMWRITERFS_QUEUE_H
#define OPENZIM_ZIMWRITERFS_QUEUE_H

#define MAX_QUEUE_SIZE 100

#include <pthread.h>
#include <unistd.h>

template<typename T>
class Queue {
    public:
        Queue() {pthread_mutex_init(&m_queueMutex,NULL);};
        virtual ~Queue() {pthread_mutex_destroy(&m_queueMutex);};
        virtual bool isEmpty();
        virtual void pushToQueue(const T& element);
        virtual bool popFromQueue(T &filename);

    protected:
        std::queue<T>   m_realQueue;
        pthread_mutex_t m_queueMutex;

    private:
        // Make this queue non copyable
        Queue(const Queue&);
        Queue& operator=(const Queue&);
};

template<typename T>
bool Queue<T>::isEmpty() {
    pthread_mutex_lock(&m_queueMutex);
    bool retVal = m_realQueue.empty();
    pthread_mutex_unlock(&m_queueMutex);
    return retVal;
}

template<typename T>
void Queue<T>::pushToQueue(const T &element) {
    unsigned int wait = 0;
    unsigned int queueSize = 0;

    do {
        usleep(wait);
        pthread_mutex_lock(&m_queueMutex);
        queueSize = m_realQueue.size();
        pthread_mutex_unlock(&m_queueMutex);
        wait += 10;
    } while (queueSize > MAX_QUEUE_SIZE);

    pthread_mutex_lock(&m_queueMutex);
    m_realQueue.push(element);
    pthread_mutex_unlock(&m_queueMutex);
}

template<typename T>
bool Queue<T>::popFromQueue(T &element) {
    pthread_mutex_lock(&m_queueMutex);
    if (m_realQueue.empty()) {
        pthread_mutex_unlock(&m_queueMutex);
        return false;
    }

    element = m_realQueue.front();
    m_realQueue.pop();
    pthread_mutex_unlock(&m_queueMutex);

  return true;
}

#endif // OPENZIM_ZIMWRITERFS_QUEUE_H