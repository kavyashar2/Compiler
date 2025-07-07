#include <iostream>
#ifndef LABEL_H
#define LABEL_H

using namespace std;

class Label // Objective 2-1
{
    static unsigned _counter;
    unsigned _number;

public:
    Label();
    unsigned number() const;
};

ostream &operator<<(ostream &ostr, const Label &label);

#endif /* LABEL_H */