#include <strings.h>  /* for bzero */
#include <errno.h>    /* for EINVAL */
#include <stdio.h>    /* for stderr */
#include <assert.h>

#undef HOOK_PREFIX
#include "hook_protos.h"

#define HOOK_PREFIX(i) __next_hook_ ## i
#include "hook_protos.h"
#undef HOOK_PREFIX

#define HIDDEN __attribute__((visibility("hidden")))
#ifndef ALLOC_EVENT_ATTRIBUTES
#define ALLOC_EVENT_ATTRIBUTES HIDDEN
#endif
#include "alloc_events.h"

/* Avoid an implicit declaration of this helper. */
size_t malloc_usable_size(void *);

#ifndef ALLOCPTR_TO_USERPTR
#define ALLOCPTR_TO_USERPTR(a) (a)
#endif

#ifndef USERPTR_TO_ALLOCPTR
#define USERPTR_TO_ALLOCPTR(u) (u)
#endif

void hook_init(void)
{
	// chain here
	ALLOC_EVENT(post_init)();
	__next_hook_init();
}

void *hook_malloc(size_t size, const void *caller)
{
	void *result;
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "called malloc(%zu)\n", size);
	#endif
	size_t modified_size = size;
	size_t modified_alignment = sizeof (void *);
	ALLOC_EVENT(pre_alloc)(&modified_size, &modified_alignment, caller);
	assert(modified_alignment == sizeof (void *));
	
	result = __next_hook_malloc(modified_size, caller);
	
	if (result) ALLOC_EVENT(post_successful_alloc)(result, modified_size, modified_alignment, 
			size, sizeof (void*), caller);
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "malloc(%zu) returned chunk at %p (modified size: %zu, userptr: %p)\n", 
		size, result, modified_size, ALLOCPTR_TO_USERPTR(result)); 
	#endif
	return ALLOCPTR_TO_USERPTR(result);
}

void hook_free(void *userptr, const void *caller)
{
	void *allocptr = USERPTR_TO_ALLOCPTR(userptr);
	#ifdef TRACE_MALLOC_HOOKS
	if (userptr != NULL) fprintf(stderr, "freeing chunk at %p (userptr %p)\n", allocptr, userptr);
	#endif 
	if (userptr != NULL) ALLOC_EVENT(pre_nonnull_free)(userptr, malloc_usable_size(allocptr));
	
	__next_hook_free(allocptr, caller);
	
	if (userptr != NULL) ALLOC_EVENT(post_nonnull_free)(userptr);
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "freed chunk at %p\n", allocptr);
	#endif
}

void *hook_memalign(size_t alignment, size_t size, const void *caller)
{
	void *result;
	size_t modified_size = size;
	size_t modified_alignment = alignment;
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "calling memalign(%zu, %zu)\n", alignment, size);
	#endif
	ALLOC_EVENT(pre_alloc)(&modified_size, &modified_alignment, caller);
	
	result = __next_hook_memalign(modified_alignment, modified_size, caller);
	
	if (result) ALLOC_EVENT(post_successful_alloc)(result, modified_size, modified_alignment, size, alignment, caller);
	#ifdef TRACE_MALLOC_HOOKS
	printf ("memalign(%zu, %zu) returned %p\n", alignment, size, result);
	#endif
	return ALLOCPTR_TO_USERPTR(result);
}


void *hook_realloc(void *userptr, size_t size, const void *caller)
{
	void *result_allocptr;
	void *allocptr = USERPTR_TO_ALLOCPTR(userptr);
	size_t alignment = sizeof (void*);
	size_t old_usable_size;
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "realigning user pointer %p (allocptr: %p) to requested size %zu\n", userptr, 
			allocptr, size);
	#endif
	/* Split cases. First we eliminate the cases where
	 * realloc() degenerates into either malloc or free. */
	if (userptr == NULL)
	{
		/* We behave like malloc(). */
		ALLOC_EVENT(pre_alloc)(&size, &alignment, caller);
	}
	else if (size == 0)
	{
		/* We behave like free(). */
		old_usable_size = malloc_usable_size(allocptr);
		ALLOC_EVENT(pre_nonnull_free)(userptr, old_usable_size);
	}
	else
	{
		/* We are doing a bone fide realloc. This might fail, leaving the
		 * original block untouched. 
		 * If it changes, we'll need to know the old usable size to access
		 * the old trailer. */
		old_usable_size = malloc_usable_size(allocptr);
		ALLOC_EVENT(pre_nonnull_nonzero_realloc)(userptr, size, caller);
	}
	
	/* Modify the size, as usual, *only if* size != 0 */
	size_t modified_size = size;
	size_t modified_alignment = sizeof (void *);
	if (size != 0)
	{
		ALLOC_EVENT(pre_alloc)(&modified_size, &modified_alignment, caller);
		assert(modified_alignment == sizeof (void *));
	}

	result_allocptr = __next_hook_realloc(allocptr, modified_size, caller);
	
	if (userptr == NULL)
	{
		/* like malloc() */
		if (result_allocptr) ALLOC_EVENT(post_successful_alloc)(result_allocptr, modified_size, modified_alignment, 
				size, sizeof (void*), caller);
	}
	else if (size == 0)
	{
		/* like free */
		ALLOC_EVENT(post_nonnull_free)(userptr);
	}
	else
	{
		/* bona fide realloc */
		ALLOC_EVENT(post_nonnull_nonzero_realloc)(userptr, modified_size, old_usable_size, caller, result_allocptr);
	}

	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "reallocated user chunk at %p, new user chunk at %p (requested size %zu, modified size %zu)\n", 
			userptr, ALLOCPTR_TO_USERPTR(result_allocptr), size, modified_size);
	#endif
	return ALLOCPTR_TO_USERPTR(result_allocptr);
}
