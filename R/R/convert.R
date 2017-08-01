setAs('numeric', 'arrow.array', function(from){
    .Call('R_double_to_arrow', from)
})


setAs('arrow.array', 'numeric', function(from){
    .Call('R_arrow_to_double', from)
})


double_to_arrow = function(x)
{
    .Call('R_double_to_arrow', as(x, 'numeric'))
}
