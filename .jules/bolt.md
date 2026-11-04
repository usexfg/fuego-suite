## 2024-07-29 - O(N) Array Allocation Removal in WalletLegacy

**Learning:** `std::accumulate` and lambdas can create significant overhead if they capture expensive copies of unused variables. In `WalletLegacy::calculateDepositsAmount` and `calculateInvestmentsAmount`, the lambda was passing the large `heights` vector by copy, even though it was completely unused inside the lambda. The `heights` vector was being explicitly built by querying transaction history over N transfers before passing it into this function, only for the lambda to throw it away.

**Action:** Look out for complex data transformations (like fetching block heights for a list of transactions) that exist *purely* to satisfy a function signature, but are never consumed by the business logic inside the function. Dead code elimination isn't just about deleting single lines; it's about removing the expensive prep work too.
