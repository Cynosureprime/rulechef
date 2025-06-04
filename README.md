# Rule Chain Generator

A powerful tool for analyzing and generating rule chains using Markov chains and probabilistic transitions. This application processes rule files to generate new valid rule combinations based on statistical analysis of existing rules.

## How it Works

### Rule Analysis
rulechef performs multi-stage analysis of input rules:

1. **Rule Validation**
   - Rules are roughly validated (full validation is not carried out)
   - Parses rules in hashcat/JtR/MDXfind format
   - Performs basic validation of operation syntax
   - Invalid rules are rejected but processing continues
   
3. **Pattern Extraction**
   - Breaks rules into individual operations (unigrams)
   - Example: The rule `$1T0` becomes two operations: `$1` and `T0`
   - Tracks operation frequencies and relationships

4. **Transition Analysis** 
   - Maps how operations follow each other
   - Creates probability matrix for operation pairs
   - Example: After `$1`, what % of time is `T0` next?
   - Currently uses 1st order Markov chains only

### Rule Generation

The generator constructs new rules by:

1. Starting with most frequent operations
2. Following probability-weighted transitions
3. Building chains up to specified maximum length
4. Applying probability thresholds to prune unlikely chains


## Basic Usage

```bash
rule_chain_generator <rulefile1> [rulefile2] [options]
```

## Command Line Options

### Core Parameters

* `-m N, --min-length N`
  - Sets the minimum rule length (in operations)
  - Default: 1
  - Range: 1-10
  - Use this to ensure generated rules meet a minimum complexity requirement

* `-M N, --max-length N`  
  - Sets the maximum rule length (in operations)
  - Default: 6
  - Range: 1-16
  - Controls the upper bound of rule complexity/length

* `-p X, --probability X`
  - Sets minimum probability threshold for rule generation
  - Default: 0.0
  - Range: 0.0-1.0 
  - Higher values (e.g. 0.01) generate more likely/common combinations
  - Lower values allow more novel but potentially less common combinations

* `-l N, --limit N`
  - Limits initial operation (begining of the chain) to top N most frequent operations
  - Transitions after the inital operation are still carried out in full
  - Useful for focusing generation on most common patterns
  - Can significantly improve performance with large rule sets

* `-v, --verbose`
  - Enables detailed output during processing
  - Shows statistics, analysis progress, and generation details
  - Helpful for understanding the tool's decision-making

* `-h, --help`
  - Displays help message with usage information

### Control Parameters

- **Probability (-p)**
  - Controls chain acceptance threshold
  - Rule generation stops as soon as probability of the next transition drops below threshold
  - Higher values (e.g. 0.1) = fewer, more likely rules
  - Lower values (e.g. 0.01) = more rules, including rarer patterns
  - Default 0.0 = generate all possible chains

- **Starting Operations (-l)**
  - Limits initial operations to top N most frequent
  - Helps focus on most effective patterns
  - Can be combined with -p for refined output
  - Example: `-l 200 -p 0.05` uses top 200 starters with 5% probability threshold

- **Length Control (-m/-M)**
  - Sets minimum (-m) and maximum (-M) operations per rule
  - Example: `T0s5s` counts as 2 operations
  - Helps target specific rule complexity levels
  - Useful for balancing coverage vs performance

## Example Usage

Basic rule generation with default settings:
```bash
rule_chain_generator rules.txt
```

Generate shorter rules with high probability:
```bash
rule_chain_generator rules.txt -M 4 -p 0.01
```

Focus on common patterns with verbose output:
```bash
rule_chain_generator rules.txt -M 3 -p 0.5 -v
```

Limit analysis to most frequent rules:
```bash 
rule_chain_generator rules.txt -M 5 -l 200 -v
```

Process multiple rule files:
```bash
rule_chain_generator rules1.txt rules2.txt -m 2 -M 5 -p 0.01
```

## Performance Considerations

- The `-l` limit option can significantly improve performance with large rule sets
- Higher probability thresholds (-p) reduce output volume and processing time
- Verbose mode (-v) helps monitor processing of large datasets
- Maximum length (-M) has substantial impact on processing time and memory usage

## Output

- Generated rules are written to stdout
- Statistics and progress information go to stderr when verbose mode is enabled
- Each generated rule appears on a new line
- Invalid rules are automatically filtered out

## Limitations

- Maximum rule length is capped at 16 operations
- Some operation combinations may be theoretically valid but practically unused
- Very low probability thresholds may generate a large volume of output

## Credits

* Members of CsP for their support
* chick3nman for the motivation
* tychotihonus for the suggestions
