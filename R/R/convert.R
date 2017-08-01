double_to_arrow = function(x)
{
    .Call('R_double_to_arrow', as(x, 'numeric'))
}
