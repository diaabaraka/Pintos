
/* x & y --> 17.14 Fixed-point numbers. */
/* n --> Integer number*/


/* Conversion */
int int_to_fixed(int n);
int fixed_to_int_round(int x);
int fixed_to_int_floor(int x);

/* Adding */
int add_two_fixed(int x,int y);
int add_fixed_int(int x,int n);

/* Subtraction */
int sub_two_fixed(int x,int y);
int sub_fixed_int(int x,int n);

/* multiplication */
int mul_two_fixed(int x,int y);
int mul_fixed_int(int x,int n);

/* Division */
int div_two_fixed(int x,int y);
int div_fixed_int(int x,int n);







/* Conversion */
int
int_to_fixed(int n)
{
    return n*(1<<14);

}
int fixed_to_int_round(int x)
{
    if(x>=0)
    {
        return (x+(1<<14)/2)/(1<<14);

    }
    /* x<0 */
    return (x-(1<<14)/2)/(1<<14);
}

int fixed_to_int_floor(int x)
{
    return x/(1<<14);
}

/* Adding */
int add_two_fixed(int x,int y)
{

    return x+y;

}
int add_fixed_int(int x,int n)
{

    return x+int_to_fixed(n);

}

/* Subtraction */
int sub_two_fixed(int x,int y)
{
    return x-y;

}
int sub_fixed_int(int x,int n)
{
    return x-int_to_fixed(n);

}

/* multiplication */
int mul_two_fixed(int x,int y)
{
    return ((int64_t)x)*y/(1<<14);

}
int mul_fixed_int(int x,int n)
{
    return x*n;
}



/* Division */
int div_two_fixed(int x,int y)
{
    return ((int64_t)x)*(1<<14)/y;

}
int div_fixed_int(int x,int n)
{
    return x/n;

}


