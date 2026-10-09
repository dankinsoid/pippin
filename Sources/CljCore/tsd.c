// @ai-generated(solo)
#include "tsd_internal.h"

#include "clj/core.h"

void clj_tsd_key_create(pthread_key_t *key, void (*destructor)(void *)) {
	if (pthread_key_create(key, destructor) != 0) clj_fatal("pthread_key_create failed");
	static char probe;
	void       *was = pthread_getspecific(*key);
	pthread_setspecific(*key, &probe);
	bool same = clj_tsd_get(*key) == &probe;
	pthread_setspecific(*key, was);
	if (!same) clj_fatal("a pthread key's slot is not where the thread register says");
}
