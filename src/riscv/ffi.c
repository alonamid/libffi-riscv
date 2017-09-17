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

//counts the number of floats and non-floats in a struct recursively.
//We need this recursive function since there may be nested structs
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

        *p_argv = (unsigned)ALIGN(*p_argv, e->alignment);

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

    //printf("FFI_RV64_SINGLE=%d\n", FFI_RV64_SINGLE);
    //printf("FFI_RV64_SOFT_FLOAT=%d\n", FFI_RV64_SOFT_FLOAT);
    //printf("FFI_RV64_DOUBLE=%d\n", FFI_RV64_DOUBLE);
    //printf("FFI_DEFAULT_ABI=%d\n", FFI_DEFAULT_ABI);
    //printf("ecif->cif->abi=%d\n", ecif->cif->abi);
    //printf("__riscv_xlen=%d\n", __riscv_xlen);
    //printf("__riscv_flen=%d\n", __riscv_flen);
    //printf("bytes is %d\n", bytes);

    ffi_cif* wcif = ecif->cif; //working cif
    char isvariadic = wcif->isvariadic;
    int nfixedargs = wcif->nfixedargs;

    int max_fp_reg_size = (wcif->abi == FFI_RV64_DOUBLE || wcif->abi == FFI_RV32_DOUBLE) ? 64 : 
                             ((wcif->abi == FFI_RV64_SOFT_FLOAT || wcif->abi == FFI_RV32_SOFT_FLOAT) ? 0 : 32); //this can be expanded to 128 for QUAD if needed

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

   // printf("ecif address %x\n", ecif);
    //return value is a hidden argument
   // printf("marker 1\n");
   // printf("marker 4\n");
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

        // printf("xreg = %d, freg = %d\n", xreg, freg);
        //printf("argument details: type: %d, size: %d, alignment: %d\n", (*p_arg)->type, (*p_arg)->size, (*p_arg)->alignment);
        //printf("argument details: argp: %x, fargp: %x\n", argp, fargp);
        int type = (*p_arg)->type;


        if (type == FFI_TYPE_STRUCT)
        {
            struct_float_counter(&num_struct_floats, &num_struct_ints, *p_arg, max_fp_reg_size);
        }


        z = (*p_arg)->size;
        //printf("p_arg type %d,p_arg_size %d, p_argv %x\n", type, z, *p_argv);
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


            if (freg<8 && (type == FFI_TYPE_FLOAT || type == FFI_TYPE_DOUBLE || num_struct_floats>0))
            {
              //if this is a floating point, we want to align the floating point "fake stack"
              if ((a - 1) & (unsigned long) argp)
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
                /* Handle structures. */
                default:
                    if (type == FFI_TYPE_STRUCT && num_struct_floats==1)
                    {
                        //memcpy(argp, *p_argv, (*p_arg)->size);
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
                /* Check if the data will fit within the register space.
                   Handle it if it doesn't. */
                unsigned long end = (unsigned long) argp + z;
                //unsigned long cap = (unsigned long) stack + bytes;
                //unsigned long cap = stack + (8 * FFI_SIZEOF_ARG);
                unsigned long cap = arg_stack_start;
                if (end <= cap) //still storing in register space
                {
                    memcpy(argp, *p_argv, z);
                    int temp_num_args = (z + FFI_SIZEOF_ARG - 1) / FFI_SIZEOF_ARG; //ceiling of number of integer regs
                    xreg += temp_num_args;
                    argp += temp_num_args * FFI_SIZEOF_ARG;
                }
                else if (argp > cap) //we're already storing on the stack
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
            memcpy(argp, *p_argv, z);
            argp += z;
        }
        p_argv++;
    }
}




//calculate and update the bytes of a struct that is passed through registers
//this is done recursively to handle the case of nested structs.
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







//calculate and update the flags of a struct that is passed through registers
//this is done recursively to handle the case of nested structs.
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





static unsigned riscv_return_struct_flags_rec(ffi_type *arg, unsigned flags)
{
    //unsigned flags = 0;
    unsigned int index = 0;
    ffi_type *e;
    

    while (e = arg->elements[index])
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


//on the first iteration, remaining bytes = arg->size
//it is later subtracted for nested structs
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
    //printf("num_struct_floats %d, num_struct_ints %d\n", num_struct_floats, num_struct_ints); 
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




/* The flags output of this routine should match the various struct cases
   described in ffitarget.h */

static unsigned calc_riscv_return_struct_flags(int soft_float, ffi_type *arg)
{
    unsigned flags = 0;
    unsigned small = FFI_TYPE_SMALLSTRUCT;
    ffi_type *e;
    
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
    
    e = arg->elements[0];
    if (e->type == FFI_TYPE_DOUBLE)
        flags = FFI_TYPE_DOUBLE;
    else if (e->type == FFI_TYPE_FLOAT)
        flags = FFI_TYPE_FLOAT;
    
    if (flags && (e = arg->elements[1]))
    {
        if (e->type == FFI_TYPE_DOUBLE)
            flags += FFI_TYPE_DOUBLE << FFI_FLAG_BITS;
        else if (e->type == FFI_TYPE_FLOAT)
            flags += FFI_TYPE_FLOAT << FFI_FLAG_BITS;
        else
            flags += FFI_TYPE_INT << FFI_FLAG_BITS;
            //return small;
        
        if (flags && (arg->elements[2]))
        {
            /* There are three arguments and the first two are
               floats! This must be passed the old way. */
            return small;
        }
        
        if (soft_float)
            flags += FFI_TYPE_STRUCT_SOFT;
    }
    else if (!flags)
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
    unsigned loc = 0;
    unsigned xcount = (cif->nargs < 8) ? cif->nargs : 8;
    unsigned fcount = (cif->nargs < 8) ? cif->nargs : 8;
    unsigned index = 0;
    
    unsigned int struct_flags = 0;
    int soft_float = cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT;;
    int soft_double = cif->abi == FFI_RV64_SINGLE || cif->abi == FFI_RV32_SINGLE;;
    unsigned int max_fp_reg_size = (cif->abi == FFI_RV64_DOUBLE || cif->abi == FFI_RV32_DOUBLE) ? 64 : 
                             ((cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT) ? 0 : 32); //this can be expanded to 128 for QUAD if needed
 
    cif->flags = 0;
    
    if (cif->rtype->type == FFI_TYPE_STRUCT)
    {
        //struct_flags = calc_riscv_return_struct_flags(soft_float, cif->rtype);
        struct_flags = riscv_return_struct_flags(max_fp_reg_size, cif->rtype);
        if (struct_flags == 0)
        {
            /* This means that the structure is being passed as
               a hidden argument */
            xarg_reg = 1;
            //count = (cif->nargs < 7) ? cif->nargs : 7;
            cif->rstruct_flag = !0;
        }
        else
            cif->rstruct_flag = 0;
    }
    else
        cif->rstruct_flag = 0;
   
    //printf("cif->rstruct_flag=%x\n",cif->rstruct_flag);

    /* Set the first 8 existing argument types in the flag bit string
     * 
     * We only describe the two argument types we care about:
     * - Whether or not its a float/double
     * - Whether or not its a struct
     * 
     * This is is two bits per argument accounting for the first 16 bits
     * of cif->flags.
     * 
     * The last 16 bits are just used to describe the return type
     * 
     * FFI_FLAG_BITS = 2
     */
    //printf("xarg_reg %x, farg_reg %x, index %x, cif->nargs %x\n", xarg_reg, farg_reg, index, cif->nargs);
    while ((xarg_reg<8 || farg_reg<8) && index < cif->nargs)
    {
        type = (cif->arg_types)[index]->type;
        //printf("xarg_reg = %d, farg_reg = %d, index = %d, nargs = %d, type=%d\n", xarg_reg, farg_reg, index, cif->nargs, type);
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
                    unsigned struct_index = 0;

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

    //printf("before setting return type cif->flags=%x\n",cif->flags);
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
   
    int soft_float = cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT;;
 
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
                unsigned num_struct_floats=0;
                unsigned num_struct_ints=0;
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

    //printf("fbytes: %d, bytes: %d, extra_bytes: %d\n", fbytes, bytes, extra_bytes);

    if (!soft_float)
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
    //printf("final bytes is %d\n", bytes);
}

/* Perform machine dependent cif processing */

ffi_status ffi_prep_cif_machdep(ffi_cif *cif)
{
    ffi_prep_cif_machdep_bytes(cif);
    ffi_prep_cif_machdep_flags(cif, 0, 0);
    cif->isvariadic = 0;
    //printf("cif flags is %x\n", cif->flags);
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

    //printf("cif rsturct = %d\n", cif->rstruct_flag);

    ecif.cif = cif;
    ecif.avalue = avalue;

    /* If the return value is a struct and we don't have a return	*/
    /* value address then we need to make one		                */

    if ((rvalue == NULL) && (cif->rtype->type == FFI_TYPE_STRUCT))
        ecif.rvalue = alloca(cif->rtype->size);
    else
        ecif.rvalue = rvalue;
   
    //printf("bytes before ffi_call_asm %d\n", cif->bytes);
    //printf("cif rsturct = %d\n", ecif.cif->rstruct_flag);
    //printf("ecif original address %x\n", &ecif);
    //printf("cif original address %x\n", cif);
 
    ffi_call_asm(ffi_prep_args, &ecif, cif->bytes, cif->flags, ecif.rvalue, fn);
}

#if FFI_CLOSURES

extern void ffi_closure_asm(void) __attribute__((visibility("hidden")));

ffi_status ffi_prep_closure_loc(ffi_closure *closure, ffi_cif *cif, void (*fun)(ffi_cif*,void*,void**,void*), void *user_data, void *codeloc)
{
    unsigned int *tramp = (unsigned int *) &closure->tramp[0];
    
    //printf("got here");
    uintptr_t fn = (uintptr_t) ffi_closure_asm;
    FFI_ASSERT(tramp == codeloc);
    
    /* Remove when more than just rv64 is supported */
    if (cif->abi != FFI_RV64_SINGLE || cif->abi != FFI_RV64_DOUBLE)
    {
        //printf("bad abi?");
       // return FFI_BAD_ABI;
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
    int fargn_org = *fargn;
    int argn_org = *argn;
    unsigned o;
    char *tp;
    char *argp;
    char *fpp;
    unsigned int ret_argn = 0; 
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
            //printf("a: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
            offset = o;
            //printf("b: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
            tp = target + offset;
            fpp = (char *) (fpr + *fargn);
            argp = (char *)(ar + *argn);
            //printf("c: o %d, arg_offset %d, argn %d, fargn %d, argp %x, fpp %x, tp %x \n", o, arg_offset, *argn, *fargn, argp, fpp, tp);
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
                //printf("c: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
                offset += elt_type->size;
                arg_offset += elt_type->size;
                //printf("d: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
                *argn += arg_offset / sizeof(ffi_arg);
                //ret_argn += elt_type->size;
                arg_offset = arg_offset % sizeof(ffi_arg);
                //printf("e: o %d, arg_offset %d, argn %d, argp %x ret_argn %d\n", o, arg_offset, argn, argp, ret_argn);
            }
            elt_typep++;
        }
        *argn = arg_offset > 0 ? (*argn)+1 : *argn;
        //printf("f: o %d, arg_offset %d, argn %d, fargn %d, argp %x, fpp %x, tp %x \n", o, arg_offset, *argn, *fargn, argp, fpp, tp);
        //printf("f: o %d, arg_offset %d, argn %d, fargn %d, argp %x \n", o, arg_offset, *argn, *fargn, argp);
    }
    else
    {
        while(*elt_typep)
        {
            ffi_type *elt_type = *elt_typep;
            o = ALIGN(offset, elt_type->alignment);
            arg_offset += o - offset;
            //printf("a: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
            offset = o;
            //printf("b: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
            tp = target + offset;
            *argn += arg_offset / sizeof(ffi_arg);
            arg_offset = arg_offset % sizeof(ffi_arg);
            argp = (char *)(ar + *argn);

            if (elt_type->type == FFI_TYPE_STRUCT)
            {
                 copy_struct(tp, offset, abi, elt_type, argn, fargn, arg_offset, ar, fpr, max_fp_reg_size);
                 //offset += struct_res;
                 offset += elt_type->size;
                 //arg_offset += struct_res;;
                 //printf("d: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
            }
            else
            {
                memcpy(tp, argp + arg_offset, elt_type->size);
                //printf("c: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
                offset += elt_type->size;
                arg_offset += elt_type->size;
                //printf("d: o %d, arg_offset %d, argn %d, argp %x\n", o, arg_offset, argn, argp);
                *argn += arg_offset / sizeof(ffi_arg);
                //ret_argn += elt_type->size;
                arg_offset = arg_offset % sizeof(ffi_arg);
                //printf("e: o %d, arg_offset %d, argn %d, argp %x \n", o, arg_offset, *argn, argp);
            }
            elt_typep++;
        }
        *argn = arg_offset > 0 ? (*argn)+1 : *argn;
        //printf("f: o %d, arg_offset %d, argn %d, argp %x \n", o, arg_offset, *argn, argp);
    }
    //printf("return val: %d\n", ret_argn - sizeof(ffi_arg)*argn_org);
    //printf("return val: %d\n", ((ret_argn + (sizeof(ffi_arg)-1)) / sizeof(ffi_arg)));
    //return ((ret_argn + (sizeof(ffi_arg)-1)) / sizeof(ffi_arg)) + argn_org;
    //printf("return val: %d\n", (argn + (arg_offset + (sizeof(ffi_arg)-1)) / sizeof(ffi_arg)));
    //return argn + (arg_offset + (sizeof(ffi_arg)-1))/sizeof(ffi_arg);
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
    int soft_float;
    int argn_struct;
    ffi_arg *argp;
    size_t z;
    
    cif = closure->cif;
    soft_float = cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT;
    unsigned int max_fp_reg_size = (cif->abi == FFI_RV64_DOUBLE || cif->abi == FFI_RV32_DOUBLE) ? 64 : 
                             ((cif->abi == FFI_RV64_SOFT_FLOAT || cif->abi == FFI_RV32_SOFT_FLOAT) ? 0 : 32); //this can be expanded to 128 for QUAD if needed
    avalue = alloca(cif->nargs * sizeof (ffi_arg));
    avaluep = alloca(cif->nargs * sizeof (ffi_arg));
    argn = 0;
    fargn = 0;
    
    //printf("ar %x, fpr %x\n", ar[0], fpr[0]);
    if (cif->rstruct_flag)
    {
        rvalue = (void *)ar[0];
        argn = 1;
    }
    
    i = 0;
    avn = cif->nargs;
    arg_types = cif->arg_types;
    
    while (i < avn)
    {
        z = arg_types[i]->size;
        //printf("size is %d\n", z);
        //the argument is a float/doubel/ Handling depends wether its variadic, what is the number of argsm and is there a soft float
        if (arg_types[i]->type == FFI_TYPE_FLOAT || arg_types[i]->type == FFI_TYPE_DOUBLE)
        {
           //printf("fpr %x, fargn %x\n", fpr[0], fargn);
           argp = (fargn >= 8 || (cif->isvariadic && (i > cif->nfixedargs)) || soft_float) ? ar + argn : fpr + fargn;
           //avaluep[i] = (char *) argp2;
           //printf("argp %x\n", argp[0]);
           //printf("argp %f\n", (char)*argp);
           switch (arg_types[i]->type)
           {
                case FFI_TYPE_FLOAT:
                    avaluep[i] = &avalue[i];
                    avalue[i] = (char*) *argp;
                    break;
                    
                case FFI_TYPE_DOUBLE:
                    avaluep[i] = &avalue[i];
                    avalue[i] = (char*) *argp;
                    break;
           }
           //printf("avalue %x\n", avalue[i]);
        }
        //long doubles are handled according to the integer convention
        else if (arg_types[i]->type == FFI_TYPE_LONGDOUBLE)
        {
            argp = ar + argn;
            if ((uintptr_t)argp & (arg_types[i]->alignment-1))
            {
                argp = (ffi_arg*)ALIGN(argp, arg_types[i]->alignment);
                //argn++;
                //argn = ALIGN(argn, arg_types[i]->alignment / sizeof(ffi_arg));
            }
            avaluep[i] = alloca(arg_types[i]->size);
            memcpy(avaluep[i], argp, arg_types[i]->size);
        }
        //handle according to struct or integer convetions
        else
        {
            unsigned type = arg_types[i]->type;
            
            if (arg_types[i]->alignment > sizeof(ffi_arg) && (arg_types[i]->size <= 2*sizeof(ffi_arg)))
                argn = ALIGN(argn, arg_types[i]->alignment / sizeof(ffi_arg));

            argp = ar + argn;
            
            /* The size of a pointer depends on the ABI */
            if (type == FFI_TYPE_POINTER)
                type = (cif->abi == FFI_RV64_SINGLE || cif->abi == FFI_RV64_DOUBLE || cif->abi == FFI_RV64_SOFT_FLOAT) ? FFI_TYPE_SINT64 : FFI_TYPE_SINT32;

            //if (soft_float && type == FFI_TYPE_FLOAT)
            //    type = FFI_TYPE_UINT32;
            
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
        if ((arg_types[i]->type == FFI_TYPE_FLOAT || arg_types[i]->type == FFI_TYPE_DOUBLE) && !(fargn >= 8 || (cif->isvariadic && (i>cif->nfixedargs) ) || soft_float))
        {
           fargn++;
        }
        else if (arg_types[i]->type == FFI_TYPE_STRUCT) 
        {
            argn = argn_struct;
        }
        else
        {
           argn += ALIGN(z, sizeof(ffi_arg)) / sizeof(ffi_arg);
           //printf("z %d. size of ffi_arg %d, argn %d\n", z, sizeof(ffi_arg), argn);
        }
        i++;
    }
    
    /* Invoke the closure. */
    (closure->fun) (cif, rvalue, avaluep, closure->user_data);
    return cif->flags >> (FFI_FLAG_BITS * 8);
}

#endif /* FFI_CLOSURES */
