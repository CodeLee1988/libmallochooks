/* Prototypes for the generic library-level hooks.
 * These are based on the glibc hooks. */
static void generic_initialize_hook(void);
static void *generic_malloc_hook(size_t size, const void *caller);
static void generic_free_hook(void *ptr, const void *caller);
static void *generic_memalign_hook(size_t alignment, size_t size, const void *caller);
static void *generic_realloc_hook(void *ptr, size_t size, const void *caller);

/* The next-in-chain hooks. */
extern void __next_initialize_hook(void) __attribute__((weak));
extern void *__next_malloc_hook(size_t size, const void *caller)__attribute__((weak));
extern void __next_free_hook(void *ptr, const void *caller) __attribute__((weak));
extern void *__next_memalign_hook(size_t alignment, size_t size, const void *caller) __attribute__((weak));
extern void *__next_realloc_hook(void *ptr, size_t size, const void *caller) __attribute__((weak));

/* Avoid an implicit declaration of this helper. */
extern size_t malloc_usable_size(void *);

static void
generic_initialize_hook(void)
{
	// chain here
	if (__next_initialize_hook) __next_initialize_hook();
	initialize_hook();
}

static void *
generic_malloc_hook(size_t size, const void *caller)
{
	void *result;
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "calling malloc(%zu)\n", size);
	#endif
	size_t modified_size = size;
	size_t modified_alignment = sizeof (void *);
	pre_alloc(&modified_size, &modified_alignment, caller);
	assert(modified_alignment == sizeof (void *));
	
	if (__next_malloc_hook) result = __next_malloc_hook(size, caller);
	else result = __real_malloc(modified_size);
	
	if (result) post_successful_alloc(result, modified_size, modified_alignment, 
			size, sizeof (void*), caller);
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "malloc(%zu) returned chunk at %p (modified size: %zu, userptr: %p)\n", 
		size, result, modified_size, allocptr_to_userptr(result)); 
	#endif
	return allocptr_to_userptr(result);
}

static void
generic_free_hook(void *userptr, const void *caller)
{
	void *allocptr = userptr_to_allocptr(userptr);
	#ifdef TRACE_MALLOC_HOOKS
	if (userptr != NULL) fprintf(stderr, "freeing chunk at %p (userptr %p)\n", allocptr, userptr);
	#endif 
	if (userptr != NULL) pre_nonnull_free(userptr, malloc_usable_size(allocptr));
	
	if (__next_free_hook) __next_free_hook(allocptr, caller);
	else __real_free(allocptr);
	
	if (userptr != NULL) post_nonnull_free(userptr);
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "freed chunk at %p\n", allocptr);
	#endif
}

static void *
generic_memalign_hook (size_t alignment, size_t size, const void *caller)
{
	void *result;
	size_t modified_size = size;
	size_t modified_alignment = alignment;
	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "calling memalign(%zu, %zu)\n", alignment, size);
	#endif
	pre_alloc(&modified_size, &modified_alignment, caller);
	
	if (__next_memalign_hook) result = __next_memalign_hook(modified_alignment, modified_size, caller);
	else result = __real_memalign(modified_alignment, modified_size);
	
	if (result) post_successful_alloc(result, modified_size, modified_alignment, size, alignment, caller);
	#ifdef TRACE_MALLOC_HOOKS
	printf ("memalign(%zu, %zu) returned %p\n", alignment, size, result);
	#endif
	return allocptr_to_userptr(result);
}


static void *
generic_realloc_hook(void *userptr, size_t size, const void *caller)
{
	void *result_allocptr;
	void *allocptr = userptr_to_allocptr(userptr);
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
		pre_alloc(&size, &alignment, caller);
	}
	else if (size == 0)
	{
		/* We behave like free(). */
		pre_nonnull_free(userptr, malloc_usable_size(allocptr));
	}
	else
	{
		/* We are doing a bone fide realloc. This might fail, leaving the
		 * original block untouched. 
		 * If it changes, we'll need to know the old usable size to access
		 * the old trailer. */
		old_usable_size = malloc_usable_size(allocptr);
		pre_nonnull_nonzero_realloc(userptr, size, caller);
	}
	
	/* Modify the size, as usual, *only if* size != 0 */
	size_t modified_size = size;
	size_t modified_alignment = sizeof (void *);
	if (size != 0)
	{
		pre_alloc(&modified_size, &modified_alignment, caller);
		assert(modified_alignment == sizeof (void *));
	}

	if (__next_realloc_hook) result_allocptr = __next_realloc_hook(allocptr, modified_size, caller);
	else result_allocptr = __real_realloc(allocptr, modified_size);
	
	if (userptr == NULL)
	{
		/* like malloc() */
		if (result_allocptr) post_successful_alloc(result_allocptr, modified_size, modified_alignment, 
				size, sizeof (void*), caller);
	}
	else if (size == 0)
	{
		/* like free */
		post_nonnull_free(userptr);
	}
	else
	{
		/* bona fide realloc */
		post_nonnull_nonzero_realloc(userptr, modified_size, old_usable_size, caller, result_allocptr);
	}

	#ifdef TRACE_MALLOC_HOOKS
	fprintf(stderr, "reallocated user chunk at %p, new user chunk at %p (requested size %zu, modified size %zu)\n", 
			userptr, allocptr_to_userptr(result_allocptr), size, modified_size);
	#endif
	return allocptr_to_userptr(result_allocptr);
}
