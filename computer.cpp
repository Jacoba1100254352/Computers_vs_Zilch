#include "computer.h"

void computer::probability(zilch &game)
{
    const unsigned FULL_SET_OF_DICE = 6;
    unsigned calculation = 0;
    std::map<unsigned, double> individualProbabilities;
    std::map<unsigned, double> multiplesProbabilities;

    for ( int i = 1; i <= FULL_SET_OF_DICE; i++  )
        individualProbabilities[i] = i/pow(game.getNumOfDiceInPlay(), game.getNumOfDiceInPlay());
}
