/* -----------------------------------------------------------------------
   ffi.c - Copyright (c) 2015 Michael Knyszek <mknyszek@berkeley.edu>
                         2015 Andrew Waterman <waterman@cs.berkeley.edu>
   Based on MIPS N32/64 port
   
   RISC-V Foreign Function Interface 

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   ``Software''), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   ----------------------------------------------------------------------- */

#include <ffi.h>
#include <ffi_common.h>

#include <stdlib.h>
#include <stdint.h>

#define STACK_ARG_SIZE(x) ALIGN(x, FFI_SIZEOF_ARG)


/* This function counts the number of floats and non-floats in a struct recursively.
   We need this recursive function since there may be nested structs, and the struct ABI
   is based on the assumption the structs are flatten to a flat hierarchy.
   max_fp_reg_size is the maximum size of a floating point register (single precision or double precision) */
static void struct_float_counter(unsigned int* num_struct_floats, unsigned int* num_struct_ints, ffi_type * p_arg, unsigned int max_fp_reg_size)
{
    ffi_type *e;
    unsigned index = 0;
    while ((e = (p_arg)->elements[index]))
    {
        if (e->type == FFI_TYPE_FLOAT && max_fp_reg_size >=32)
        {
            (*num_struct_floats)++;
        }
        else if (e->type == FFI_TYPE_DOUBLE && max_fp_reg_size >= 64)
        {
            (*num_struct_floats)++;
        }
        else if (e->type == FFI_TYPE_STRUCT)
        {
            struct_float_counter(num_struct_floats, num_struct_ints, e, max_fp_reg_size);
        }
        else
        {
            (*num_struct_ints)++;
        }
        index++;
    }

}


/* This function takes in pointers to argument lists and locations, and places arguments from a flattened struct into appropriate
  integer and floating point registers. */
static void struct_args_to_regs(ffi_type * p_arg, char** argp, char** fargp, void ** p_argv, int* xreg, int* freg, unsigned int * a)
{
    ffi_type *e;
    unsigned index = 0;
    while ((e = (p_arg)->elements[index]))
    {
        if (e->type == FFI_TYPE_DOUBLE || e->type == FFI_TYPE_FLOAT)
        {
            if ((*a - 1) & (unsigned long) *argp)
            {
                *fargp = (char *) ALIGN(*fargp, *a);
            }
        }

        *p_argv = (void*) ALIGN(*p_argv, e->alignment);

        switch (e->type)
        {
            case FFI_TYPE_FLOAT:
                *(float *) *fargp = *(float *)(* p_argv);
                (*freg)++;
                (*fargp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_DOUBLE:
                *(double *) *fargp = *(double *)(* p_argv);
                (*freg)++;
                (*fargp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_SINT8:
                *(ffi_arg *) *argp = *(SINT8 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_UINT8:
                *(ffi_arg *) *argp = *(UINT8 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_SINT16:
                *(ffi_arg *) *argp = *(SINT16 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_UINT16:
                *(ffi_arg *) *argp = *(UINT16 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_SINT32:
                *(ffi_arg *) *argp = *(SINT32 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_UINT32:
                *(ffi_arg *) *argp = *(UINT32 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;
                    
            case FFI_TYPE_SINT64:
                *(ffi_arg *) *argp = *(SINT64 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_UINT64:
                *(ffi_arg *) *argp = *(UINT64 *)(* p_argv);
                (*xreg)++;
                (*argp) += FFI_SIZEOF_ARG;
                break;

            case FFI_TYPE_STRUCT:
               struct_args_to_regs(p_arg, argp, fargp, p_argv, xreg, freg, a);
               break;

            default:
               break; 
         }
         index++;
         (*p_argv)+= e->size;
    }
}



/* ffi_prep_args is called by the assembly routine once stack space
   has been allocated for the function's arguments */
static void ffi_prep_args(char *stack, extended_cif *ecif, int bytes, int flags)
{
    int i;
    void **p_argv;
    char *argp, *fargp, *cpy_struct, *arg_stack_start;
    ffi_type **p_arg;   

    ffi_cif* wcif = ecif->cif; //working cif
    char isvariadic = wcif->isvariadic;
    int nfixedargs = wcif->nfixedargs;
 
    //max_fp_reg_size is the maximim size of a floating point register, depending if the ABI defined it as single precision (32) or double precision (64)
    //This can be expanded to 128 bits for QUAD precision if needs
    int max_fp_reg_size = (wcif->abi == FFI_RV64_DOUBLE || wcif->abi == FFI_RV32_DOUBLE) ? 64 : 
                             ((wcif->abi == FFI_RV64_SOFT_FLOAT || wcif->abi == FFI_RV32_SOFT_FLOAT) ? 0 : 32);

    //counters for used integer and floating point registers
    int xreg = 0;
    int freg = 0;

 
    fargp = stack;
    if (max_fp_reg_size != 0)
    {  
      argp = stack + (8* FFI_SIZEOF_ARG);
      arg_stack_start = stack + (16 * FFI_SIZEOF_ARG); //set the "fake stack" space for arguments that are going to be passed through registers
    }
    else
    {
        argp = stack;
        arg_stack_start = stack + (8 * FFI_SIZEOF_ARG); //set the "fake stack" space for arguments that are going to be passed through registers
    } 
    cpy_struct = stack + ALIGN(bytes, 16);

    memset(stack, 0, bytes);

    if (wcif->rstruct_flag != 0)
    {
        *(ffi_arg *) argp = (ffi_arg) ecif->rvalue;
        argp += sizeof(ffi_arg);
        xreg++;
    }
    
    p_argv = ecif->avalue;

    for (i = 0, p_arg = wcif->arg_types; i < wcif->nargs; i++, p_arg++)
    {
        size_t z;
        unsigned int a;
	unsigned int num_struct_floats = 0;
        unsigned int num_struct_ints = 0;


        /* Align if necessary. */
        a = (*p_arg)->alignment;
        if (a < sizeof(ffi_arg))
            a = sizeof(ffi_arg);

        int type = (*p_arg)->type;


        if (type == FFI_TYPE_STRUCT)
        {
            struct_float_counter(&num_struct_floats, &num_struct_ints, *p_arg, max_fp_reg_size);
        }


        z = (*p_arg)->size;
        if (z <= sizeof(ffi_arg) && (freg < 8 || xreg <8))
        { 
            int type = (*p_arg)->type;
            z = sizeof(ffi_arg);

            /* The size of a pointer depends on the ABI */
            if (type == FFI_TYPE_POINTER)
            #if _riscv_xlen == 64
                type = FFI_TYPE_SINT64;
            #else
                type = FFI_TYPE_SINT32;
            #endif



            /* Handle float argument types for soft float case */ 
            if (xreg<8 && ((max_fp_reg_size < 32) || freg>7 || (isvariadic && i>=nfixedargs)))
            {
                switch (type)
                {
                    case FFI_TYPE_FLOAT:
                        type = FFI_TYPE_UINT32;
                        break;
                    default:
                        break;
                }
            }
        

            /* Handle double argument types for single precision soft case */ 
            if (xreg<8 && ((max_fp_reg_size < 64) || freg>7 || (isvariadic && i>=nfixedargs)))
            {
                switch (type)
                {
                    case FFI_TYPE_DOUBLE:
                        type = FFI_TYPE_UINT64;
                        break;
                    default:
                        break;
                }
            }


            /* Handle float argument types into single registers */ 
            if (freg<8 && (type == FFI_TYPE_FLOAT || type == FFI_TYPE_DOUBLE || num_struct_floats>0) && !(isvariadic && i>=nfixedargs))
            {
              //if this is a floating point, we want to align the floating point "fake stack"
              if ((a - 1) & (unsigned long) fargp)
              {
                  fargp = (char *) ALIGN(fargp, a);
              }

              switch (type)
              {
                /* This can only happen with 64bit slots. */
                case FFI_TYPE_FLOAT:
                    *(float *) fargp = *(float *)(* p_argv);
                    if (freg < 8) {freg++; fargp += FFI_SIZEOF_ARG;}
                    break;

                case FFI_TYPE_DOUBLE:
                    *(double *) fargp = *(double *)(* p_argv);
                    if (freg < 8) {freg++; fargp += FFI_SIZEOF_ARG;}
                    break;
                /* Handle structures with floating point elements */
                default:
                    if (type == FFI_TYPE_STRUCT && num_struct_floats==1)
                    {
                        struct_args_to_regs(*p_arg, &argp, &fargp, p_argv, &xreg, &freg, &a);
                    }
                    else if (type == FFI_TYPE_STRUCT && (num_struct_floats == 2) && (max_fp_reg_size != 0) && freg<7)
                    {   
                        struct_args_to_regs(*p_arg, &argp, &fargp, p_argv, &xreg, &freg, &a);
                    }
                    else if (type == FFI_TYPE_STRUCT && (num_struct_floats == 1) && (num_struct_ints == 1) && (max_fp_reg_size != 0) && freg<8 && xreg<8)
                    {    
                        struct_args_to_regs(*p_arg, &argp, &fargp, p_argv, &xreg, &freg, &a);
                    }
                    break;
              }
            }
            else //handle like an integer
            {
              if ((a - 1) & (unsigned long) argp)
              {
                  argp = (char *) ALIGN(argp, a);
              }
              switch (type)
              {
                case FFI_TYPE_SINT8:
                    *(ffi_arg *)argp = *(SINT8 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;

                case FFI_TYPE_UINT8:
                    *(ffi_arg *)argp = *(UINT8 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;

                case FFI_TYPE_SINT16:
                    *(ffi_arg *)argp = *(SINT16 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;

                case FFI_TYPE_UINT16:
                    *(ffi_arg *)argp = *(UINT16 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;

                case FFI_TYPE_SINT32:
                    *(ffi_arg *)argp = *(SINT32 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;

                case FFI_TYPE_UINT32:
                    *(ffi_arg *)argp = *(UINT32 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;
                    
                case FFI_TYPE_SINT64:
                    *(ffi_arg *)argp = *(SINT64 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;

                case FFI_TYPE_UINT64:
                    *(ffi_arg *)argp = *(UINT64 *)(* p_argv);
                    if (xreg<8) xreg++;
                    break;
                /* Handle structures. */
                default:
                    memcpy(argp, *p_argv, (*p_arg)->size);
                    if (xreg<8) xreg++;
                    break;
               }
               argp += z;
            } 
        }
        else if (z <= 2*sizeof(ffi_arg) && (freg < 8 || xreg <8))
        {
            if (type == FFI_TYPE_STRUCT && (num_struct_floats == 2) && (num_struct_ints==0) && (max_fp_reg_size != 0) && freg<7)
            {   
               struct_args_to_regs(*p_arg, &argp, &fargp, p_argv, &xreg, &freg, &a);
            }

            else if (type == FFI_TYPE_STRUCT && (num_struct_floats == 1) && (num_struct_ints == 1) && (max_fp_reg_size != 0) && freg<8 && xreg<8)
            {    
               struct_args_to_regs(*p_arg, &argp, &fargp, p_argv, &xreg, &freg, &a);
            }
            else //handle like the integer convention
            {
               if (isvariadic && i>=nfixedargs && xreg<8 && a==2*FFI_SIZEOF_ARG && (xreg%2)==1)
               {
                  xreg += 1;
                  argp += FFI_SIZEOF_ARG; 
               }
                /* Check if the data will fit within the register space.
                   Handle it if it doesn't. */
                unsigned long end = (unsigned long) argp + z;
                unsigned long cap = (unsigned long) arg_stack_start;
                if (end <= cap) //still storing in register space
                {
                    memcpy(argp, *p_argv, z);
                    int temp_num_args = (z + FFI_SIZEOF_ARG - 1) / FFI_SIZEOF_ARG; //ceiling of number of integer regs
                    xreg += temp_num_args;
                    argp += temp_num_args * FFI_SIZEOF_ARG;
                }
                else if ((unsigned long)argp > cap) //we're already storing on the stack
                {
                    if ((a - 1) & (unsigned long) argp)
                    {
                        argp = (char *) ALIGN(argp, a);
                    }

                    memcpy(argp, *p_argv, z);
                    argp += z;
                }
                else //need to store partially in register space and partially on the stack
                {
                    unsigned long portion = cap - (unsigned long)argp;
                    memcpy(argp, *p_argv, portion);
                    xreg += (portion + FFI_SIZEOF_ARG - 1) / FFI_SIZEOF_ARG; //ceiling of number of integer regs
                    argp = arg_stack_start;
                    z -= portion;
                    memcpy(argp, (void*)((unsigned long)(*p_argv) + portion), z);
                    argp += z;
                }
            }
        }
        else if(xreg < 8 && z > 2*sizeof(ffi_arg))
        {
            /* It's too big to pass in any registers or on the stack, 
               so we pass a pointer, and copy the struct to pass by value.
               But, we can't _just_ copy it onto the stack! We need to actually
               make sure it gets onto the "bottom" (really the top, high memory
               addresses) of the stack frame... */
            /* Update pointer to where our struct location on the stack is */
            cpy_struct -= ALIGN(z, a);
            
            memcpy(cpy_struct, *p_argv, z);
            
            /* Pass pointer in register */
            *(ffi_arg *)argp = (ffi_arg) cpy_struct;
            xreg++; 
            z = sizeof(ffi_arg);
            argp += FFI_SIZEOF_ARG;
        }
        else
        {
            /* Just some big struct, pass it by value by copying it onto
               the stack. */
            argp = (char *) ALIGN(argp, a);
            memcpy(argp, *p_argv, z);
            argp += z;
        }
        p_argv++;
    }
}




/*calculate and update the overall size in bytes in register 
   of a struct that is passed through registers
   This is done recursively to handle the case of nested structs. */
static void riscv_struct_bytes(unsigned int* fbytes, unsigned int* bytes, ffi_type *s_arg)
{
    ffi_type *e;
    unsigned struct_index = 0;
    while ((e = s_arg->elements[struct_index]))
    {
        if (e->type == FFI_TYPE_DOUBLE || e->type == FFI_TYPE_FLOAT)
        {
            if ((e->alignment - 1) & *fbytes)
            {
                *fbytes = (unsigned)ALIGN(*fbytes, e->alignment);
            }
            *fbytes += STACK_ARG_SIZE(e->size);
        }
        else if (e->type == FFI_TYPE_STRUCT)
        {
            riscv_struct_bytes(fbytes, bytes,e);
        }
        else
        {
            /* Add any padding if necessary */
            if ((e->alignment - 1) & *bytes)
            {
                *bytes = (unsigned)ALIGN(*bytes, e->alignment);
            }
            *bytes += STACK_ARG_SIZE(e->size);
        }
        struct_index++;
    }

}


/* calculate and update the flags of a struct that is passed through registers
   This is done recursively to handle the case of nested structs. */
static void riscv_struct_flags(unsigned int* farg_reg, unsigned int* xarg_reg, unsigned int* temp_float_flags, unsigned int* temp_int_flags, ffi_type *s_arg, unsigned int max_fp_reg_size)
{
    int struct_index=0;
    ffi_type *e;
    while ((e = s_arg->elements[struct_index]))
    {
        if (e->type == FFI_TYPE_DOUBLE && max_fp_reg_size >= 64)
        {
            *temp_float_flags += 1 << (*farg_reg);
            (*farg_reg)++;
        }
        else if (e->type == FFI_TYPE_FLOAT && max_fp_reg_size >= 32)
        {
            *temp_float_flags += 0 << (*farg_reg);
            (*farg_reg)++;
        }
        else if (e->type == FFI_TYPE_STRUCT)
        {
            riscv_struct_flags(farg_reg, xarg_reg, temp_float_flags, temp_int_flags, e, max_fp_reg_size); 
        }
        else
        {
            *temp_int_flags += 0 << (*xarg_reg);
            (*xarg_reg)++;
        }
        struct_index++;
    }
}


/* The recursive function call to determine the flags for a return value
   which is a struct.
   Requires recursive function since ABI assumes struct hierarchies are flattened */
static unsigned riscv_return_struct_flags_rec(ffi_type *arg, unsigned flags)
{
    unsigned int index = 0;
    ffi_type *e;
    

    while ((e = arg->elements[index]))
    {
        if (e->type == FFI_TYPE_DOUBLE)
            flags += FFI_TYPE_DOUBLE << (index*FFI_FLAG_BITS);
        else if (e->type == FFI_TYPE_FLOAT)
            flags += FFI_TYPE_FLOAT << (index*FFI_FLAG_BITS);
        else if (e->type == FFI_TYPE_STRUCT)
            flags += riscv_return_struct_flags_rec(e, flags) << (index*FFI_FLAG_BITS);
        else
            flags += FFI_TYPE_INT << (index*FFI_FLAG_BITS);
        index++;
    }
    return flags;
}


/* Function to determine the flags for a return value which is a struct */
static unsigned riscv_return_struct_flags(int max_fp_reg_size, ffi_type *arg)
{
    unsigned flags = 0;
    unsigned small = FFI_TYPE_SMALLSTRUCT;
    
    /* Returning structures under n32 is a tricky thing.
       A struct with only one or two floating point fields
       is returned in $f0 (and $f2 if necessary). Any other
       struct results at most 128 bits are returned in $2
       (the first 64 bits) and $3 (remainder, if necessary).
       Larger structs are handled normally. */
    
    if (arg->size > 2 * FFI_SIZEOF_ARG)
        return 0;
    
    if (arg->size > FFI_SIZEOF_ARG)
        small = FFI_TYPE_SMALLSTRUCT2;
   

    unsigned int num_struct_floats =0;
    unsigned int num_struct_ints =0; 
    struct_float_counter(&num_struct_floats, &num_struct_ints, arg, max_fp_reg_size);
    if ((num_struct_floats==1) && (num_struct_ints ==0) && (max_fp_reg_size !=0))
    {
        flags = riscv_return_struct_flags_rec(arg, flags);
    }
    else if (num_struct_floats == 2 && (num_struct_ints==0) && (max_fp_reg_size != 0))
    {   
        flags = riscv_return_struct_flags_rec(arg, flags);
    }
    else if ((num_struct_floats == 1) && (num_struct_ints == 1) && (max_fp_reg_size != 0))
    {    
        flags = riscv_return_struct_flags_rec(arg, flags);
    }
    else //if (flags && (num_struct_floats > 2 || num_struct_ints > 2))
    {
        /* There are three arguments and the first two are
               floats! This must be passed the old way. */
        return small;
    }
        
    if (max_fp_reg_size ==0)
        flags += FFI_TYPE_STRUCT_SOFT;
    
    if (!flags)
        return small;
    
    return flags;
}



/* Generate the flags word for processing arguments and 
   putting them into their proper registers in the 
   assembly routine. */

void ffi_prep_cif_machdep_flags(ffi_cif *cif, unsigned int isvariadic, unsigned int nfixedargs)
{
    int type;
    unsigned xarg_reg = 0;
    unsigned farg_reg = 0;
    unsigned temp_float_flags =0;
    unsigned temp_int_flags= 0;
    unsigned index = 0;
    
    unsigned int struct_flags = 0;
    unsigned int max_fp_reg_size = (cif->abi == FFI_RV64_DOUBLE || cif->abi == FFI_RV32_DOUBLE) ? 64 : 
                             ((cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT) ? 0 : 32); 
    //this can be expanded to 128 for QUAD if needed
 
    cif->flags = 0;
    
    if (cif->rtype->type == FFI_TYPE_STRUCT)
    {
        struct_flags = riscv_return_struct_flags(max_fp_reg_size, cif->rtype);
        if (struct_flags == 0)
        {
            /* This means that the structure is being passed as
               a hidden argument */
            xarg_reg = 1;
            cif->rstruct_flag = !0;
        }
        else
            cif->rstruct_flag = 0;
    }
    else
        cif->rstruct_flag = 0;
   

    /* Set the first 8 integer and first 8 floating point  existing argument types in the flag bit string
     * 
     * We describe the two argument types we care about:
     * - Whether or not its a float/double
     * - Whether or not its a struct
     *
     * For the function arguments, each of the first 16 bits of cif->flags represents 1 argument
     * The first 8 bits represent the 8 floating point registers.
     * a '0' means the argument is a float, while a '1' means the argument is a double.
     * The next 8 bits represent the 8 integer register. They are currently all 0, but have
     * potential for differentiation 
     * 
     * The last 16 bits are just used to describe the return type
     * The return type uses complex 2-bit flags, which are described in ffitarget.h
     * 
     * FFI_FLAG_BITS = 2
     */
    while ((xarg_reg<8 || farg_reg<8) && index < cif->nargs)
    {
        type = (cif->arg_types)[index]->type;
        switch (type)
        {
            case FFI_TYPE_FLOAT:  /* float_flag = 0 */
                if (farg_reg < 8 && max_fp_reg_size >= 32 && !(isvariadic && xarg_reg >= nfixedargs)) 
                {
                    temp_float_flags += 0 << (farg_reg);
                    farg_reg++;
                }
                else if (xarg_reg < 8)
                {
                    temp_int_flags += 0 << (xarg_reg);
                    xarg_reg++;
                }
                break;
            case FFI_TYPE_DOUBLE: /* float_flag = 1 */
                if (farg_reg < 8 && max_fp_reg_size >= 64 && !(isvariadic && xarg_reg >= nfixedargs)) 
                {
                    temp_float_flags += 1 << (farg_reg);
                    farg_reg++;
                }
                else if (xarg_reg < 8)
                {
                    temp_int_flags += 0 << (xarg_reg);
                    xarg_reg++;
                }
                break;
            case FFI_TYPE_LONGDOUBLE: /* goes in integer registers */
                if (xarg_reg < 7)
                {
                    temp_int_flags += 0 << (xarg_reg+1);
                    xarg_reg+=2;
                }
                else if (xarg_reg < 8)
                {
                    temp_int_flags += 0 << (xarg_reg);
                    xarg_reg++;                    
                }
                break;
            case FFI_TYPE_STRUCT:
                if(((cif->arg_types)[index]->size > 2 * FFI_SIZEOF_ARG) && xarg_reg < 8)
                /* The struct is too big to pass on the stack, so we pass it by reference */
                {
                    (xarg_reg)++;
                }
                else if ((max_fp_reg_size == 0) && xarg_reg < 8)
                {
                    int temp_num_regs = ((cif->arg_types)[index]->size + 1 / FFI_SIZEOF_ARG);
                    xarg_reg += (xarg_reg + temp_num_regs < 8) ? temp_num_regs : (8-xarg_reg);
                }
                else //we might have floating points in the struct
                {
                    unsigned num_struct_floats=0;
                    unsigned num_struct_ints=0;

                    struct_float_counter(&num_struct_floats, &num_struct_ints, cif->arg_types[index], max_fp_reg_size);
                    if (num_struct_floats == 2 && num_struct_ints ==0 && farg_reg < 7)
                    {
                        riscv_struct_flags(&farg_reg, &xarg_reg, &temp_float_flags, &temp_int_flags,cif->arg_types[index], max_fp_reg_size);
                    }
                    else if (num_struct_floats == 1 && num_struct_ints==1 && xarg_reg < 8 && farg_reg < 8)
                    {
                        riscv_struct_flags(&farg_reg, &xarg_reg, &temp_float_flags, &temp_int_flags,cif->arg_types[index], max_fp_reg_size);
                    }
                    else if (xarg_reg < 8)
                    {
                        int temp_num_regs = ((cif->arg_types)[index]->size + 1 / FFI_SIZEOF_ARG);
                        xarg_reg += (xarg_reg + temp_num_regs < 8) ? temp_num_regs : (8-xarg_reg);
                    }
               }
        }
        index++;
    }
   
    cif->flags += temp_int_flags << 8 ; 
    cif->flags += temp_float_flags;

    /* Set the return type flag */
    
    type = cif->rtype->type;
    
    /* Handle float return types for soft float case */
    if (max_fp_reg_size < 32 && type == FFI_TYPE_FLOAT)
    {
        type = FFI_TYPE_UINT32;
    }
    
    if (max_fp_reg_size < 64 && type == FFI_TYPE_DOUBLE)
    {
        type = FFI_TYPE_UINT64;
    }

    switch (type)
    {
        case FFI_TYPE_STRUCT:
            if (struct_flags != 0)
            {
                /* The structure is returned via some tricky mechanism */
                cif->flags += FFI_TYPE_STRUCT << (FFI_FLAG_BITS * 8);
                cif->flags += struct_flags << (4 + (FFI_FLAG_BITS * 8));
            }
            /* else the structure is returned through a hidden
               first argument. Do nothing, 'cause FFI_TYPE_VOID is 0 */
            break;
        case FFI_TYPE_VOID:
            /* Do nothing, 'cause FFI_TYPE_VOID is 0 */
            break;
        case FFI_TYPE_FLOAT:
        case FFI_TYPE_DOUBLE:
        case FFI_TYPE_LONGDOUBLE:
            cif->flags += cif->rtype->type << (FFI_FLAG_BITS * 8);
            break;
        case FFI_TYPE_SINT32:
        case FFI_TYPE_UINT32:
            cif->flags += FFI_TYPE_SINT32 << (FFI_FLAG_BITS * 8);
            break;
        default:
            cif->flags += FFI_TYPE_INT << (FFI_FLAG_BITS * 8);
            break;
    }
}

/* Count how big our argspace is in bytes. Here, we always
   allocate at least 8 pointer words and handle big structs
   being passed in registers. */

void ffi_prep_cif_machdep_bytes(ffi_cif *cif)
{
    int i;
    ffi_type **ptr;
    unsigned bytes = 0, fbytes =0, extra_bytes = 0;
   
    unsigned int max_fp_reg_size = (cif->abi == FFI_RV64_DOUBLE || cif->abi == FFI_RV32_DOUBLE) ? 64 : 
                             ((cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT) ? 0 : 32); 

    if (cif->rtype->type == FFI_TYPE_STRUCT)
        bytes = STACK_ARG_SIZE(sizeof(void*));
    
    for (ptr = cif->arg_types, i = 0; i < cif->nargs; i++, ptr++)
    {
        unsigned type = (cif->arg_types)[i]->type;
        /* When we pass big structs in registers, we copy it onto the stack and assign a pointer to it */
        if ((*ptr)->size > 2 * FFI_SIZEOF_ARG && bytes < 8 * FFI_SIZEOF_ARG)
        {
            bytes += sizeof(void*);
            extra_bytes += STACK_ARG_SIZE((*ptr)->size);
        }
        else
        {
            if (type == FFI_TYPE_FLOAT || type == FFI_TYPE_DOUBLE)
            {
                if (((*ptr)->alignment - 1) & fbytes)
                {
                   fbytes = (unsigned)ALIGN(fbytes, (*ptr)->alignment);
                }
                fbytes += STACK_ARG_SIZE((*ptr)->size);
            }
            else if (type == FFI_TYPE_STRUCT) //This struct must have a size of less than 2XLEN
            {
                riscv_struct_bytes(&fbytes, &bytes, cif->arg_types[i]);
            }
            else
            {
                if (((*ptr)->alignment - 1) & bytes)
                {
                   bytes = (unsigned)ALIGN(bytes, (*ptr)->alignment);
                }
                bytes += STACK_ARG_SIZE((*ptr)->size);
            }
        }
    }


    if (max_fp_reg_size != 0)
    {
        if (fbytes < 8 * FFI_SIZEOF_ARG)
        {
            fbytes = 8 * FFI_SIZEOF_ARG;
        }
    }
    if (bytes < 8 * FFI_SIZEOF_ARG)
        bytes = 8 * FFI_SIZEOF_ARG;
   
    bytes += fbytes;
    bytes += extra_bytes;   
 
    cif->bytes = bytes;
}

/* Perform machine dependent cif processing */

ffi_status ffi_prep_cif_machdep(ffi_cif *cif)
{
    ffi_prep_cif_machdep_bytes(cif);
    ffi_prep_cif_machdep_flags(cif, 0, 0);
    cif->isvariadic = 0;
    return FFI_OK;
}

/* Perform machine dependent cif processing when we have a variadic function */

ffi_status ffi_prep_cif_machdep_var(ffi_cif *cif, unsigned int nfixedargs, unsigned int ntotalargs)
{
    ffi_prep_cif_machdep_bytes(cif);
    ffi_prep_cif_machdep_flags(cif, 1, nfixedargs);
    cif->isvariadic = 1;
    cif->nfixedargs = nfixedargs;
    return FFI_OK;
}

/* Low level routine for calling RV64 functions */
extern int ffi_call_asm(void (*)(char *, extended_cif *, int, int), 
                         extended_cif *, unsigned, unsigned, 
                         unsigned *, void (*)(void))
                         __attribute__((visibility("hidden")));

void ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue)
{
    extended_cif ecif;

    ecif.cif = cif;
    ecif.avalue = avalue;

    /* If the return value is a struct and we don't have a return	*/
    /* value address then we need to make one		                */

    if ((rvalue == NULL) && (cif->rtype->type == FFI_TYPE_STRUCT))
        ecif.rvalue = alloca(cif->rtype->size);
    else
        ecif.rvalue = rvalue;
 
    ffi_call_asm(ffi_prep_args, &ecif, cif->bytes, cif->flags, ecif.rvalue, fn);
}

#if FFI_CLOSURES

extern void ffi_closure_asm(void) __attribute__((visibility("hidden")));

ffi_status ffi_prep_closure_loc(ffi_closure *closure, ffi_cif *cif, void (*fun)(ffi_cif*,void*,void**,void*), void *user_data, void *codeloc)
{
    unsigned int *tramp = (unsigned int *) &closure->tramp[0];
    
    uintptr_t fn = (uintptr_t) ffi_closure_asm;
    FFI_ASSERT(tramp == codeloc);
    
    /* Remove when more than just rv64 is supported */
    if (!(cif->abi == FFI_RV64_SINGLE || cif->abi == FFI_RV64_DOUBLE))
    {
       return FFI_BAD_ABI;
    }

    if (cif->abi == FFI_RV32_SINGLE || cif->abi == FFI_RV32_DOUBLE || cif->abi == FFI_RV32_SOFT_FLOAT || fn < 0x7ffff000U)
    {
        /* auipc t0, 0 (i.e. t0 <- codeloc) */
        tramp[0] = 0x00000297;
        /* lui t1, %hi(fn) */
        tramp[1] = 0x00000337 | ((fn + 0x800) & 0xFFFFF000);
        /* jalr x0, t1, %lo(fn) */
        tramp[2] = 0x00030067 | ((fn & 0xFFF) << 20);
        /* nops */
        tramp[3] = 0x00000013;
        tramp[4] = 0x00000013;
        tramp[5] = 0x00000013;
    }
    else
    {
        /* auipc t0, 0 (i.e. t0 <- codeloc) */
        tramp[0] = 0x00000297;
        /* ld t1, 16(t0) */
        tramp[1] = 0x0102b303;
        /* jalr x0, t1, %lo(fn) */
        tramp[2] = 0x00030067;
        /* nop */
        tramp[3] = 0x00000013;
        /* fn */
        tramp[4] = fn;
        tramp[5] = fn >> 32;
    }
    
    closure->cif = cif;
    closure->fun = fun;
    closure->user_data = user_data;
    __builtin___clear_cache(codeloc, codeloc + FFI_TRAMPOLINE_SIZE);
    
    return FFI_OK;
}

static void copy_struct(char *target, unsigned offset, ffi_abi abi, ffi_type *type, int* argn, int* fargn, unsigned arg_offset, ffi_arg *ar, ffi_arg *fpr, int max_fp_reg_size)
{
    ffi_type **elt_typep = type->elements;
    unsigned o;
    char *tp;
    char *argp;
    char *fpp;
    unsigned int num_struct_floats=0;
    unsigned int num_struct_ints=0;        
    struct_float_counter(&num_struct_floats, &num_struct_ints, type, max_fp_reg_size);
    if ((num_struct_floats ==2 && num_struct_ints==0 && *fargn < 7) ||
            (num_struct_floats ==1 && num_struct_ints==1 && *argn<8 && *fargn<8) ||
            (num_struct_floats ==1 && num_struct_ints==0 && *fargn<8))
    {
        while(*elt_typep)
        {
            ffi_type *elt_type = *elt_typep;
            o = ALIGN(offset, elt_type->alignment);
            arg_offset += o - offset;
            offset = o;
            tp = target + offset;
            fpp = (char *) (fpr + *fargn);
            argp = (char *)(ar + *argn);
            if (elt_type->type == FFI_TYPE_FLOAT && (max_fp_reg_size != 0))
            {
                *(float *)tp = *(float *)fpp;
                (*fargn)++;
                offset += elt_type->size;
            }
            else if (elt_type->type == FFI_TYPE_DOUBLE && (max_fp_reg_size !=0))
            {
                 *(double *)tp = *(double *)fpp;
                 (*fargn)++;
                offset += elt_type->size;
            }  
            else if (elt_type->type == FFI_TYPE_STRUCT && (max_fp_reg_size !=0))
            {
                 copy_struct(tp, offset, abi, elt_type, argn, fargn, arg_offset, ar, fpr, max_fp_reg_size);
            }
            else
            {
                memcpy(tp, argp + arg_offset, elt_type->size);
                offset += elt_type->size;
                arg_offset += elt_type->size;
                *argn += arg_offset / sizeof(ffi_arg);
                arg_offset = arg_offset % sizeof(ffi_arg);
            }
            elt_typep++;
        }
        *argn = arg_offset > 0 ? (*argn)+1 : *argn;
    }
    else
    {
        while(*elt_typep)
        {
            ffi_type *elt_type = *elt_typep;
            o = ALIGN(offset, elt_type->alignment);
            arg_offset += o - offset;
            offset = o;
            tp = target + offset;
            *argn += arg_offset / sizeof(ffi_arg);
            arg_offset = arg_offset % sizeof(ffi_arg);
            argp = (char *)(ar + *argn);

            if (elt_type->type == FFI_TYPE_STRUCT)
            {
                 copy_struct(tp, offset, abi, elt_type, argn, fargn, arg_offset, ar, fpr, max_fp_reg_size);
                 offset += elt_type->size;
            }
            else
            {
                memcpy(tp, argp + arg_offset, elt_type->size);
                offset += elt_type->size;
                arg_offset += elt_type->size;
                *argn += arg_offset / sizeof(ffi_arg);
                arg_offset = arg_offset % sizeof(ffi_arg);
            }
            elt_typep++;
        }
        *argn = arg_offset > 0 ? (*argn)+1 : *argn;
    }
}


/*
* Decodes the arguments to a function, which will be stored on the
* stack. AR is the pointer to the beginning of the integer
* arguments. FPR is a pointer to the area where floating point
* registers have been saved.
*
* RVALUE is the location where the function return value will be
* stored. CLOSURE is the prepared closure to invoke.
*
* This function should only be called from assembly, which is in
* turn called from a trampoline.
*
* Returns the function return flags.
*
*/
int ffi_closure_riscv_inner(ffi_closure *closure, void *rvalue, ffi_arg *ar, ffi_arg *fpr)
{
    ffi_cif *cif;
    void **avaluep;
    ffi_arg *avalue;
    ffi_type **arg_types;
    int i, avn, argn, fargn;
    int argn_struct;
    ffi_arg *argp;
    ffi_arg *fargp;
    size_t z;
    
    cif = closure->cif;
    unsigned int max_fp_reg_size = (cif->abi == FFI_RV64_DOUBLE || cif->abi == FFI_RV32_DOUBLE) ? 64 : 
                             ((cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT) ? 0 : 32); 
    //this can be expanded to 128 for QUAD if needed
    

    avalue = alloca(cif->nargs * sizeof (ffi_arg));
    avaluep = alloca(cif->nargs * sizeof (ffi_arg));
    argn = 0;
    fargn = 0;
    
    if (cif->rstruct_flag)
    {
        rvalue = (void *)ar[0];
        argn = 1;
    }
   
    argp = ar + argn; 
    i = 0;
    avn = cif->nargs;
    arg_types = cif->arg_types;
    
    while (i < avn)
    {
        z = arg_types[i]->size;
        //the argument is a float/double. Handling depends on whether it is variadic, what is the number of args, and if it is a soft float
        if (arg_types[i]->type == FFI_TYPE_FLOAT || arg_types[i]->type == FFI_TYPE_DOUBLE)
        {
           fargp = (fargn >= 8 || (cif->isvariadic && (i > cif->nfixedargs)) || (max_fp_reg_size==0)) ? argp : fpr + fargn;
           switch (arg_types[i]->type)
           {
                case FFI_TYPE_FLOAT:
                    avaluep[i] = &avalue[i];
                    avalue[i] = (void*) *fargp;
                    break;
                    
                case FFI_TYPE_DOUBLE:
                    avaluep[i] = &avalue[i];
                    avalue[i] = (void*) *fargp;
                    break;
           }
        }
        //long doubles are handled according to the integer convention
        else if (arg_types[i]->type == FFI_TYPE_LONGDOUBLE)
        {
            if ((uintptr_t)argp & (arg_types[i]->alignment-1))
            {
                argp = (ffi_arg*)ALIGN(argp, arg_types[i]->alignment);
            }
            avaluep[i] = alloca(arg_types[i]->size);
            memcpy(avaluep[i], argp, arg_types[i]->size);
        }
        //handle according to struct or integer convetions
        else
        {
            unsigned type = arg_types[i]->type;
            
           
            /* The size of a pointer depends on the ABI */
            if (type == FFI_TYPE_POINTER)
                type = (cif->abi == FFI_RV64_SINGLE || cif->abi == FFI_RV64_DOUBLE || cif->abi == FFI_RV64_SOFT_FLOAT) ? FFI_TYPE_SINT64 : FFI_TYPE_SINT32;
            

            if (arg_types[i]->alignment > sizeof(ffi_arg) && (arg_types[i]->size <= 2*sizeof(ffi_arg)))
            {
                argn = ALIGN(argn, arg_types[i]->alignment / sizeof(ffi_arg));
                argp = (void*) ALIGN(argp, arg_types[i]->alignment / sizeof(ffi_arg));
            }

            
            switch (type)
            {
                case FFI_TYPE_SINT8:
                    avaluep[i] = &avalue[i];
                    *(SINT8 *) &avalue[i] = (SINT8) *argp;
                    break;
                    
                case FFI_TYPE_UINT8:
                    avaluep[i] = &avalue[i];
                    *(UINT8 *) &avalue[i] = (UINT8) *argp;
                    break;
                    
                case FFI_TYPE_SINT16:
                    avaluep[i] = &avalue[i];
                    *(SINT16 *) &avalue[i] = (SINT16) *argp;
                    break;
                    
                case FFI_TYPE_UINT16:
                    avaluep[i] = &avalue[i];
                    *(UINT16 *) &avalue[i] = (UINT16) *argp;
                    break;
                    
                case FFI_TYPE_SINT32:
                    avaluep[i] = &avalue[i];
                    *(SINT32 *) &avalue[i] = (SINT32) *argp;
                    break;
                    
                case FFI_TYPE_UINT32:
                    avaluep[i] = &avalue[i];
                    *(UINT32 *) &avalue[i] = (UINT32) *argp;
                    break;
                    
                case FFI_TYPE_SINT64:
                    avaluep[i] = &avalue[i];
                    *(SINT64 *) &avalue[i] = (SINT64) *argp;
                    break;
                    
                case FFI_TYPE_UINT64:
                    avaluep[i] = &avalue[i];
                    *(UINT64 *) &avalue[i] = (UINT64) *argp;
                    break;
                    
                case FFI_TYPE_STRUCT:
                    if (argn < 8 && arg_types[i]->size <= 2*sizeof(ffi_arg))
                    {
                        /* Allocate space to copy structs that were passed in registers */
                        avaluep[i] = alloca(arg_types[i]->size);
                        argn_struct = argn;
                        copy_struct(avaluep[i], 0, cif->abi, arg_types[i], &argn_struct, &fargn, 0, ar, fpr, max_fp_reg_size);
                        break;
                    }
                    else
                    {
                        /* The struct was too big to be passed in registers, so it was passed on the stack 
                           with pointers in the registers. We need to properly pass the pointer AND set
                           the correct size to increment by! */
                        avaluep[i] = (void *) *argp;
                        //z = 1;
                        z = sizeof(ffi_arg);
                        argn_struct = argn + ALIGN(z, sizeof(ffi_arg)) / sizeof(ffi_arg);
                        break;
                    }
                    
                /* Else fall through. */
                default:
                    avaluep[i] = (char *) argp;
                    break;
            }
        }
        if ((arg_types[i]->type == FFI_TYPE_FLOAT || arg_types[i]->type == FFI_TYPE_DOUBLE) && !(fargn >= 8 || (cif->isvariadic && (i>cif->nfixedargs) ) || (max_fp_reg_size == 0)))
        {
           fargn++;
        }
        else if (arg_types[i]->type == FFI_TYPE_STRUCT) 
        {
            argp += (argn_struct - argn);
            argn = argn_struct;
        }
        else
        {
           argn += ALIGN(z, sizeof(ffi_arg)) / sizeof(ffi_arg);
           argp += ALIGN(z, sizeof(ffi_arg)) / sizeof(ffi_arg);
        }
        i++;
    }
   
    /* Invoke the closure. */
    (closure->fun) (cif, rvalue, avaluep, closure->user_data);
    return cif->flags >> (FFI_FLAG_BITS * 8);
}

#endif /* FFI_CLOSURES */
