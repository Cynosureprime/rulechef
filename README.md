
# rulechef - a markov rule generator

Analyses a ruleset for hashcat/mdxfind/john the ripper and iteratively generates rules based on markov chains created from the input rules. Users can specify additional parameters such as the min/max rule operations, chain probability and starting chain limits.

 This tool was written in an attempt to craft better quality rules as opposed to the usual rule stacking or randomly generating rules.
  
## How it works

The user provides a list of rules in hashcat/mdxfind/jtr format, rule are roughly validated (full validation is not carried), invalid rules will be rejected while valid rules will be analysed with each rule operation forming a unigram ($1 - suffix the number '1', would be seen as a unigram). Additional data is also collected for transitions, eg going from rule operation 1 > rule operation 2, along with bigrams (two rule operations) and trigrams (three rule operations). Currently only 1st order markov chains are used therefore the bigram and trigram data is not used yet.

The generator then uses the unigrams and transitions to iteratively generate the links forming the chains of rule operations, with the most common chains being output first for each number of rule operations.

Users have the ability to adjust the probability parameter '-p' which governs the probability of each chain derived from the transitions. This would mean specifying a very high '-p' would result in very little rules, while not specifying '-p' which defaults to 0 would mean generate all chains. Depending on the input of your rule list play around with '-p 0.01' or even '-p 0.1' or maybe something in between.

Additionally users have access to the '-l' switch which governs the starting unigrams, since the starting unigrams are ordered by popularity users have the option to use the TopN unigrams and discard the rest. You can also use '-l' with '-p' together.

The '-m' or min and '-M' max govern the number of rule operations to generate, so you can skip and limit the number of rule ops in your output. Eg the rule 'T0s5s' would be 2 rule operations.

Please keep in mind that this is the first iteration of this tool, so is far from perfect, depending on it's usefulness and reception from the community it may further be improved.

## Use cases

* Rule Discovery
* Rule Expansion (set -p to 0)
* Rule Contraction (set -p > 0)

## Usage examples


Generate **all** rules with **3 operations** to **8 operations** print statistics and progress while generating
```
rulechef rules.txt -m 3 -M 8 -v > out.txt
```

Generate chains of rules with a **probability upto 0.01** consisting of **4 operations** to **6 operations**, print statistics and progress while generating
```
rulechef rules.txt -m 4 -M 6 -p 0.01 -v > out.txt
```

Generate all rules but limit the start of the chain link to **top 100** common, subsequent links will not be limited, **do not** output stats
```
rulechef rules.txt -l 100 > out.txt
```
 
## Technologies

* markov chains
* hashmaps
* chain pruning
  
## Credits

* Members of CsP for their support
* chick3nman for the motivation
* tychotihonus for the suggestions
